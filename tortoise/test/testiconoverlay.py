#
# development and stub-testing module for overlay icon handlers
#

import os, sys, time, atexit
from mercurial import ui
from mercurial.i18n import _

# FIXMEL: quick & dirty hack to add tortoise to module search path
import __init__
moddir = os.path.dirname(__init__.__file__)
sys.path.insert(0, os.path.join(moddir, os.pardir))

from iconoverlay import IconOverlayExtension

def lsprof(checkargs):
    try:
        from mercurial import lsprof
    except ImportError:
        raise util.Abort(_(
            'lsprof not available - install from '
            'http://codespeak.net/svn/user/arigo/hack/misc/lsprof/'))
    p = lsprof.Profiler()
    p.enable(subcalls=True)
    try:
        return checkargs()
    finally:
        p.disable()
        stats = lsprof.Stats(p.getstats())
        stats.sort()
        stats.pprint(top=10, file=sys.stderr, climit=5)
        
def profile(checkargs):        
    import hotshot, hotshot.stats
    prof = hotshot.Profile("hg.prof")
    try:
        try:
            return prof.runcall(checkargs)            
        except:
            try:
                ui.warn(_('exception raised - generating '
                         'profile anyway\n'))
            except:
                pass
            raise
    finally:
        prof.close()
        stats = hotshot.stats.load("hg.prof")
        stats.strip_dirs()
        stats.sort_stats('cumulative', 'time', 'calls')
        stats.print_stats(40)
        
def timeit():
    u = ui.ui()
    def get_times():
        t = os.times()
        if t[4] == 0.0: # Windows leaves this as zero, so use time.clock()
            t = (t[0], t[1], t[2], t[3], time.clock())
        return t
    s = get_times()
    def print_time():
        t = get_times()
        u.warn(_("Time: real %.3f secs (user %.3f+%.3f sys %.3f+%.3f)\n") %
            (t[4]-s[4], t[0]-s[0], t[2]-s[2], t[1]-s[1], t[3]-s[3]))
            
    atexit.register(print_time)
    
def get_option(args):
    import getopt
    long_opt_list = ('time', 'lsprof', 'profile')
    opts, args = getopt.getopt(args, "", long_opt_list)
    options = {}
    for o, a in opts:
        options[o] = a
    return options, args


if __name__=='__main__':
    ovh = IconOverlayExtension()

    option, argv = get_option(sys.argv[1:])
    path = len(argv) and argv[0] or os.getcwd()
    path = os.path.abspath(path)
    
    # get the list of files residing in the target directory
    dirname = os.path.dirname(path)
    if dirname == path:
        dirlist = [path]
    else:
        dirlist = [os.path.join(dirname, x) for x in os.listdir(dirname)]

    # first call to _get_state() is usually longer...
    def get_state_1st():
        ovh._get_state(path)

    # subsequent call to _get_state() using the files in
    # the target directory
    def get_state_2nd():
        for f in dirlist:
            ovh._get_state(f)
        
    # 'master' function for profiling purpose
    def get_states():
        get_state_1st()
        get_state_2nd()
     
    if option.has_key('--time'):
        timeit()
        
    if option.has_key('--lsprof'):
        lsprof(get_states)
    elif option.has_key('--profile'):
        profile(get_states)
    else:
        get_states()
