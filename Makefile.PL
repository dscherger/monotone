use 5.008005;
use strict;

use IO::File;

my($dest_dir,
   $file_comparison,
   $globs_file,
   $html_viewer,
   $lib_dir,
   $makefile,
   $no_use_dists_mas,
   $prefix_dir);
my %valid_options = (DESTDIR          => \$dest_dir,
		     FILE_COMPARISON  => \$file_comparison,
		     GLOBS_FILE       => \$globs_file,
		     HTML_VIEWER      => \$html_viewer,
		     LIBDIR           => \$lib_dir,
		     NO_USE_DISTS_MAS => \$no_use_dists_mas,
		     PREFIX           => \$prefix_dir);

# Parse command line options.

foreach my $arg (@ARGV)
{
    my($name,
       $value);
    ($name, $value) = ($arg =~ m/^([^=]+)=(.*)$/);
    $name = uc($name);
    if (exists($valid_options{$name}))
    {
	${$valid_options{$name}} = $value;
    }
    else
    {
	print(STDERR
	      "usage: perl Makefile.PL [PREFIX=<PATH>] [DESTDIR=<PATH>] "
	          . "[LIBDIR=<PATH>]\n"
	          . "                        [GLOBS_FILE=<PATH>] "
	          . "[HTML_VIEWER=<PATH>]\n"
	          . "                        [FILE_COMPARISON=<PATH>] "
	          . "[NO_USE_DISTS_MAS=1]\n");
	exit(1);
    }
}

# Check that the dependencies are satisfied.

if (defined($no_use_dists_mas) && $no_use_dists_mas != 0)
{
    system("perl linux-installer --dep-level=1 --no-use-dists-mas");
}
else
{
    system("perl linux-installer --dep-level=1");
}

# Generate Makefile.

print("Writing Makefile for mtn-browse\n");
die("IO::File failed with $!")
    if (! defined($makefile = IO::File->new("Makefile", "w")));
$makefile->print(<<EOF
DESTDIR="$dest_dir"
FILE_COMPARISON="$file_comparison"
GLOBS_FILE="$globs_file"
HTML_VIEWER="$html_viewer"
LIBDIR="$lib_dir"
PREFIX="$prefix_dir"
INSTALLER=./linux-installer

all: \$(INSTALLER)
	\@echo "Ok."

test: \$(INSTALLER)
	\@echo "Ok."

install: \$(INSTALLER)
EOF
);
$makefile->print("\t\$(INSTALLER)");
$makefile->print(" --destdir=\$(DESTDIR)") if (defined($dest_dir));
$makefile->print(" --file-comparison=\$(FILE_COMPARISON)")
    if (defined($file_comparison));
$makefile->print(" --globs-file=\$(GLOBS_FILE)") if (defined($globs_file));
$makefile->print(" --html-viewer=\$(HTML_VIEWER)") if (defined($html_viewer));
$makefile->print(" --libdir=\$(LIBDIR)") if (defined($lib_dir));
$makefile->print(" --no-use-dists-mas")
    if (defined($no_use_dists_mas) && $no_use_dists_mas != 0);
$makefile->print(" --prefix=\$(PREFIX)") if (defined($prefix_dir));
$makefile->print("\n");
$makefile->close();

exit(0);
