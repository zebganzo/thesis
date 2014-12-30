import os
import stat

from .executable import Executable

class FTcat(Executable):
    '''Used to wrap the ftcat binary in the Experiment object.'''

    def __init__(self, ft_cat_bin, stdout_file, stderr_file, dev, events, cpu=None):
        '''Extends the Executable initializer method with ftcat attributes.'''
        super(FTcat, self).__init__('/usr/bin/taskset')

        self.stdout_file = stdout_file
        self.stderr_file = stderr_file

        mode = os.stat(dev)[stat.ST_MODE]
        if not mode & stat.S_IFCHR:
            raise Exception("%s is not a character device" % dev)

        if events is None:
            raise Exception('No events!')

        if cpu is not None:
            # Execute only on the given CPU
            self.extra_args = ['-c', str(cpu)]
        else:
            # Execute on any cpu
            self.extra_args = ['0xFFFFFFFF']

        events_str_arr = map(str, events)
        ft_cat_cmd = [ft_cat_bin, dev] + list(events_str_arr)

        self.extra_args.extend(ft_cat_cmd)

