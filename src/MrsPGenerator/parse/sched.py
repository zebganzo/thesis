import config.config as conf
import os
import re
import struct
import subprocess

from collections import defaultdict,namedtuple
from common import recordtype,log_once
from point import Measurement
from ctypes import *

class TimeTracker:
    '''Store stats for durations of time demarcated by sched_trace records.'''
    def __init__(self):
        self.begin = self.avg = self.max = self.num = self.next_job = 0

        # Count of times the job in start_time matched that in store_time
        self.matches = 0
        # And the times it didn't
        self.disjoints = 0

        # Measurements are recorded in store_ time using the previous matching
        # record which was passed to store_time. This way, the last record for
        # any task is always skipped
        self.last_record = None

    def store_time(self, next_record):
        '''End duration of time.'''
        dur = (self.last_record.when - self.begin) if self.last_record else -1

        if self.next_job == next_record.job:
            self.last_record = next_record

            if self.last_record:
                self.matches += 1

            if dur > 0:
                self.max  = max(self.max, dur)
                self.avg *= float(self.num / (self.num + 1))
                self.num += 1
                self.avg += dur / float(self.num)

                self.begin = 0
                self.next_job   = 0
        else:
            self.disjoints += 1

    def start_time(self, record, time = None):
        '''Start duration of time.'''
        if self.last_record:
            if not time:
                self.begin = self.last_record.when
            else:
                self.begin = time

        self.next_job = record.job

# Data stored for each task
TaskParams = namedtuple('TaskParams',  ['wcet', 'period', 'cpu'])
TaskData   = recordtype('TaskData',    ['params', 'jobs', 'blocks', 'misses'])

# Map of event ids to corresponding class and format
record_map = {}

RECORD_SIZE   = 24
NSEC_PER_MSEC = 1000000

def bits_to_bytes(bits):
    '''Includes padding'''
    return bits / 8 + (1 if bits%8 else 0)

def field_bytes(fields):
    fbytes = 0
    fbits  = 0
    for f in fields:
        flist = list(f)

        if len(flist) > 2:
            # Specified a bitfield
            fbits += flist[2]
        else:
            # Only specified a type, use types size
            fbytes += sizeof(list(f)[1])

            # Bitfields followed by a byte will cause any incomplete
            # bytes to be turned into full bytes
            fbytes += bits_to_bytes(fbits)
            fbits   = 0

    fbytes += bits_to_bytes(fbits)
    return fbytes + fbits

def register_record(id, clazz):
    fields = clazz.FIELDS
    diff = RECORD_SIZE - field_bytes(SchedRecord.FIELDS) - field_bytes(fields)

    # Create extra padding fields to make record the proper size
    # Creating one big field of c_uint64 and giving it a size of 8*diff
    # _should_ work, but doesn't. This is an uglier way of accomplishing
    # the same goal
    for d in range(diff):
        fields += [("extra%d" % d, c_char)]

    # Create structure with fields and methods of clazz
    clazz2 = type("Dummy%d" % id, (LittleEndianStructure,clazz),
                  {'_fields_': SchedRecord.FIELDS + fields,
                   '_pack_'  : 1})
    record_map[id] = clazz2

def make_iterator(fname):
    '''Iterate over (parsed record, processing method) in a
    sched-trace file.'''
    if not os.path.getsize(fname):
        # Likely a release master CPU
        return

    f = open(fname, 'rb')

    while True:
        data = f.read(RECORD_SIZE)

        try:
            type_num = struct.unpack_from('b',data)[0]
        except struct.error:
            break

        if type_num not in record_map:
            continue

        clazz = record_map[type_num]
        obj = clazz()
        obj.fill(data)

        if obj.job != 1:
            yield obj
        else:
            # Results from the first job are nonsense
            pass

def read_data(task_dict, fnames):
    '''Read records from @fnames and store per-pid stats in @task_dict.'''
    buff = []

    def get_time(record):
        return record.when if hasattr(record, 'when') else 0

    def add_record(itera):
        # Ordered insertion into buff
        try:
            arecord = itera.next()
        except StopIteration:
            return

        i = 0
        for (i, (brecord, _)) in enumerate(buff):
            if get_time(brecord) > get_time(arecord):
                break
        buff.insert(i, (arecord, itera))

    for fname in fnames:
        itera = make_iterator(fname)
        add_record(itera)

    while buff:
        record, itera = buff.pop(0)

        add_record(itera)
        record.process(task_dict)

class SchedRecord(object):
    # Subclasses will have their FIELDs merged into this one
    FIELDS = [('type', c_uint8),  ('cpu', c_uint8),
              ('pid',  c_uint16), ('job', c_uint32)]

    def fill(self, data):
        memmove(addressof(self), data, RECORD_SIZE)

    def process(self, task_dict):
        raise NotImplementedError()

class ParamRecord(SchedRecord):
    FIELDS = [('wcet', c_uint32),  ('period', c_uint32),
              ('phase', c_uint32), ('partition', c_uint8)]

    def process(self, task_dict):
        params = TaskParams(self.wcet, self.period, self.partition)
        task_dict[self.pid].params = params

class ReleaseRecord(SchedRecord):
    FIELDS = [('when', c_uint64), ('release', c_uint64)]

    def process(self, task_dict):
        data = task_dict[self.pid]
        data.jobs += 1
        if data.params:
            data.misses.start_time(self, self.when + data.params.period)

class CompletionRecord(SchedRecord):
    FIELDS = [('when', c_uint64)]

    def process(self, task_dict):
        task_dict[self.pid].misses.store_time(self)

class BlockRecord(SchedRecord):
    FIELDS = [('when', c_uint64)]

    def process(self, task_dict):
        task_dict[self.pid].blocks.start_time(self)

class ResumeRecord(SchedRecord):
    FIELDS = [('when', c_uint64)]

    def process(self, task_dict):
        task_dict[self.pid].blocks.store_time(self)

# Map records to sched_trace ids (see include/litmus/sched_trace.h
register_record(2, ParamRecord)
register_record(3, ReleaseRecord)
register_record(7, CompletionRecord)
register_record(8, BlockRecord)
register_record(9, ResumeRecord)

def create_task_dict(data_dir, work_dir = None):
    '''Parse sched trace files'''
    bin_files   = conf.FILES['sched_data'].format(".*")
    output_file = "%s/out-st" % work_dir

    task_dict = defaultdict(lambda :
                            TaskData(None, 1, TimeTracker(), TimeTracker()))

    bin_names = [f for f in os.listdir(data_dir) if re.match(bin_files, f)]
    if not len(bin_names):
        return task_dict

    # Save an in-english version of the data for debugging
    # This is optional and will only be done if 'st_show' is in PATH
    if conf.BINS['st_show']:
        cmd_arr = [conf.BINS['st_show']]
        cmd_arr.extend(bin_names)
        with open(output_file, "w") as f:
            subprocess.call(cmd_arr, cwd=data_dir, stdout=f)

    # Gather per-task values
    bin_paths = ["%s/%s" % (data_dir,f) for f in bin_names]
    read_data(task_dict, bin_paths)

    return task_dict

LOSS_MSG = """Found task missing more than %d%% of its scheduling records.
These won't be included in scheduling statistics!"""%(100*conf.MAX_RECORD_LOSS)
SKIP_MSG = """Measurement '%s' has no non-zero values.
Measurements like these are not included in scheduling statistics.
If a measurement is missing, this is why."""

def extract_sched_data(result, data_dir, work_dir):
    task_dict = create_task_dict(data_dir, work_dir)
    stat_data = defaultdict(list)

    # Group per-task values
    for tdata in task_dict.itervalues():
        if not tdata.params:
            # Currently unknown where these invalid tasks come from...
            continue

        miss = tdata.misses

        record_loss = float(miss.disjoints)/(miss.matches + miss.disjoints)
        stat_data["record-loss"].append(record_loss)

        if record_loss > conf.MAX_RECORD_LOSS:
            log_once(LOSS_MSG)
            continue

        miss_ratio = float(miss.num) / miss.matches
        avg_tard = miss.avg * miss_ratio

        stat_data["miss-ratio" ].append(miss_ratio)

        stat_data["max-tard"].append(miss.max / tdata.params.period)
        stat_data["avg-tard"].append(avg_tard / tdata.params.period)

        stat_data["avg-block"].append(tdata.blocks.avg / NSEC_PER_MSEC)
        stat_data["max-block"].append(tdata.blocks.max / NSEC_PER_MSEC)

    # Summarize value groups
    for name, data in stat_data.iteritems():
        if not data or not sum(data):
            log_once(SKIP_MSG, SKIP_MSG % name)
            continue
        result[name] = Measurement(str(name)).from_array(data)
