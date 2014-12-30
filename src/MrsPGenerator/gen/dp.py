from __future__ import division

class DesignPointGenerator(object):
    '''Iterates over all combinations of values specified in options.
    Shamelessly stolen (and simplified) from bcw.'''
    def __init__(self, options):
        self.point_idx = 0  # Current point
        self.options = options
        self.total = 1
        for x in options.itervalues():
            self.total *= len(x)

    def __iter__(self):
        return self

    def next(self):
        while True:
            if self.point_idx == self.total:
                raise StopIteration
            else:
                point = {}

                divisor = 1
                for key in sorted(self.options.keys()):
                    size = len(self.options[key])

                    option_idx = int(self.point_idx / divisor) % size
                    point[key] = self.options[key][option_idx]

                    divisor *= size
                self.point_idx += 1

                return point
