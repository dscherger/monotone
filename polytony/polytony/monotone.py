import os
import fcntl
import os.path
import select
import popen2
import sha
import shutil
import errno

from polytony.bibblebabble import hex_to_bibble
import polytony.bibblefilter

import polytony.graphs

class TreeVersion(object):

    def __init__(self, hex_version=None, path=None):

        assert hex_version or path
        if hex_version:
            self.__in_db = 1
            self.__hex = hex_version
            self.__bibble = hex_to_bibble(hex_version)
            self.__short_name = "-".join(self.__bibble.split("-")[:2])
        else:
            self.__in_db = 0
            self.__path = path
            long_name = version.path()
            dirname, basename = os.path.split(long_name)
            rest, immed_dirname = os.path.split(dirname)
            if rest:
                more = "..."
            else:
                more = ""
            self.__short_name = os.path.join(more, immed_dirname, basename)


    def __str__(self):

        if self.__in_db:
            return self.__bibble
        else:
            return self.__path


    def in_db(self):

        return self.__in_db


    def hex_version(self):

        assert self.__in_db
        return self.__hex


    def version(self):

        assert self.__in_db
        return self.__bibble


    def path(self):

        assert not self.__in_db
        return self.__path


    def short_name(self):
        """A short convenient name, for display purposes."""

        return self.__short_name


    def __eq__(self, other):

        if not isinstance(other, TreeVersion):
            return 0

        if self.__in_db != other.__in_db:
            return 0

        if self.__in_db:
            return self.__hex == other.__hex
        else:
            return os.path.normpath(self.__path) \
                   == os.path.normpath(other.__path)


    def __hash__(self):

        if self.__in_db:
            return hash(self.__hex)
        else:
            return hash(self.__path)


class ExecutionError(Exception):

    def __init__(self, command, error, stdout, stderr):

        Exception.__init__(self, " ".join(command) + ": " + stderr)
        self.command = command
        self.error = error
        self.stdout = stdout
        self.stderr = stderr



class Monotone(object):
    """A class representing a Monotone database.

    Allows manipulation and querying."""


    def __init__(self, db="monotone.db", executable="monotone"):

        self.__dbpath = db
        self.__executable = executable
        self.__branch = None


    def set_branch(self, branch):
        # Pass 'None' to unset the default branch.
        
        self.__branch = branch


    def get_full_ancestry(self):

        stdout, stderr = self._run_monotone("agraph")

        skip_len = len('edge: { sourcename : "')

        graph = polytony.graphs.Graph()

        lines = stdout.split("\n")
        lines.reverse()
        while lines:
            line = lines.pop()
            if line.startswith("edge:"):
                source_id = line[skip_len:skip_len + 40]
                source = TreeVersion(hex_version=source_id)
                line = lines.pop()
                target_id = line[skip_len:skip_len + 40]
                target = TreeVersion(hex_version=target_id)
                graph.add_node(source)
                graph.add_node(target)
                graph.add_edge(source, target)
        
        return graph


    def get_cert_text(self, version):

        if not version.in_db():
            return "<working directory>"

        stdout, stderr = self._run_monotone("ls", "certs", "manifest",
                                            version.hex_version())
        return polytony.bibblefilter.filter_string(stdout)


    def is_working_dir(self, dir):

        return os.path.isdir(os.path.join(dir, "MT"))


    def get_working_dir_parent(self, tree):

        assert not tree.in_db()
        manifest = os.path.join(tree.path(), "MT", "manifest")
        digest = sha.new()
        for line in file(manifest, "r"):
            digest.update(line)
        return hex_to_bibble(digest.hexdigest())


    def checkout(self, version, destdir):

        if not version.in_db():
            shutil.copytree(version.path(), destdir)
        else:
            # Monotone is anal about where it will check out to (no path
            # containing .., no absolute path, etc....).  So we always check
            # out the the current directory.
            old_wd = os.getcwd()
            try:
                os.makedirs(destdir)
            except OSError, e:
                if e.errno == errno.EEXIST:
                    pass
                else:
                    raise
            os.chdir(destdir)
            try:
                self._run_monotone("checkout", version.hex_version(), ".")
            finally:
                os.chdir(old_wd)
        

    def diff(self, version1, version2):

        assert version1.in_db()
        
        args = ["diff", version1.hex_version()]

        if version2.in_db():
            args.append(version2.hex_version())
            old_pwd = None
        else:
            old_pwd = os.getpwd()
            os.chdir(version2.get_path())

        try:
            stdout, stderr = self._run_monotone(*args)
        finally:
            if old_pwd:
                os.chdir(old_pwd)

        return stdout


    def _run_monotone(self, *args):
        """Returns a tuple (stdout, stderr)'.

        If an error is encountered, throws an 'ExecutionError'.  'stdout' and
        'stderr' are strings."""

        command_line = [self.__executable,
                        "--db=" + self.__dbpath]
        if self.__branch is not None:
            command_line.append("--branch=" + self.__branch)

        command_line += args

        child = popen2.Popen3(command_line, 1)
        child.tochild.close()

        stdout, stderr = [], []

        for pipe in child.fromchild, child.childerr:
            fcntl.fcntl(pipe.fileno(), fcntl.F_SETFL, os.O_NONBLOCK)

        while 1:
            rlist = [child.fromchild, child.childerr]
            if rlist:
                ready = select.select(rlist, [], [])[0]

            if child.fromchild in ready:
                stdout.append(child.fromchild.read())
            if child.childerr in ready:
                stderr.append(child.childerr.read())

            status = child.poll()

            if status != -1:
                
                # Child has exited, drain pipes one last time.
                stdout.append(child.fromchild.read())
                stderr.append(child.childerr.read())

                stdout_str = "".join(stdout)
                stderr_str = "".join(stderr)

                if (os.WIFSTOPPED(status) or os.WIFSIGNALED(status)
                    or (os.WIFEXITED(status) and os.WEXITSTATUS(status))):
                    raise ExecutionError(command_line,
                                         status,
                                         stdout_str, stderr_str)
                else:
                    return (stdout_str, stderr_str)

