from __future__ import division
import random

def uniform_int(minval, maxval):
    "Create a function that draws ints uniformly from {minval, ..., maxval}"
    def _draw():
        return random.randint(minval, maxval)
    return _draw

def uniform(minval, maxval):
    "Create a function that draws floats uniformly from [minval, maxval]"
    def _draw():
        return random.uniform(minval, maxval)
    return _draw

def bernoulli(p):
    "Create a function that flips a weight coin with probability p"
    def _draw():
        return random.random() < p
    return _draw

def uniform_choice(choices):
    "Create a function that draws uniformly elements from choices"
    selector = uniform_int(0, len(choices) - 1)
    def _draw():
        return choices[selector()]
    return _draw

def truncate(minval, maxval):
    def _limit(fun):
        def _f(*args, **kargs):
            val = fun(*args, **kargs)
            return min(maxval, max(minval, val))
        return _f
    return _limit

def redraw(minval, maxval):
    def _redraw(dist):
        def _f(*args, **kargs):
            in_range = False
            while not in_range:
                val = dist(*args, **kargs)
                in_range = minval <= val <= maxval
            return val
        return _f
    return _redraw

def exponential(minval, maxval, mean, limiter=redraw):
    """Create a function that draws floats from an exponential
    distribution with expected value 'mean'. If a drawn value is less
    than minval or greater than maxval, then either another value is
    drawn (if limiter=redraw) or the drawn value is set to minval or
    maxval (if limiter=truncate)."""
    def _draw():
        return random.expovariate(1.0 / mean)
    return limiter(minval, maxval)(_draw)

def multimodal(weighted_distributions):
    """Create a function that draws values from several distributions
    with probability according to the given weights in a list of
    (distribution, weight) pairs."""
    total_weight = sum([w for (d, w) in weighted_distributions])
    selector = uniform(0, total_weight)
    def _draw():
        x = selector()
        wsum = 0
        for (d, w) in weighted_distributions:
            wsum += w
            if wsum >= x:
                return d()
        assert False # should never drop off
    return _draw

def uniform_slack(min_slack_ratio, max_slack_ratio):
    """Choose deadlines uniformly such that the slack
       is within [cost + min_slack_ratio * (period - cost),
                  cost + max_slack_ratio * (period - cost)].
                  
        Setting max_slack_ratio = 1 implies constrained deadlines.
    """
    def choose_deadline(cost, period):
        slack = period - cost
        earliest = slack * min_slack_ratio
        latest   = slack * max_slack_ratio
        return cost + random.uniform(earliest, latest)
    return choose_deadline
