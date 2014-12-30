import os

class ProcEntry(object):
    def __init__(self, proc, data):
        self.proc = proc
        self.data = data

        if not os.path.exists(self.proc):
            raise ValueError("Invalid proc entry %s" % self.proc)

    def write_proc(self):
        try:
            with open(self.proc, 'w') as entry:
                entry.write(self.data)
        except:
            print("Failed to write into %s value:\n%s" % (self.proc, self.data))
