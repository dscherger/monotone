use 5.008005;
use strict;

use IO::File;

my($dest_dir,
   $makefile,
   $prefix_dir);
my %valid_options = (DESTDIR => \$dest_dir,
		     PREFIX  => \$prefix_dir);

$dest_dir = $prefix_dir = "";

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
	      "usage: perl Makefile.PL [PREFIX=<PATH>] [DESTDIR<PATH]\n");
	exit(1);
    }
}

# Check that the dependencies are satisfied.

system("perl linux-installer --dep-level=1");

# Generate Makefile.

print("Writing Makefile for mtn-browse\n");
die("IO::File failed with $!")
    if (! defined($makefile = IO::File->new("Makefile", "w")));
$makefile->print(<<EOF
DESTDIR=$dest_dir
PREFIX=$prefix_dir
INSTALLER=linux-installer

all: \$(INSTALLER)
	\@echo "Ok."

test: \$(INSTALLER)
	\@echo "Ok."

install: \$(INSTALLER)
EOF
);
$makefile->print("	\$(INSTALLER)");
$makefile->print(" --prefix=\$(PREFIX)") if ($prefix_dir ne "");
$makefile->print(" --destdir=\$(DESTDIR)") if ($dest_dir ne "");
$makefile->print("\n");
$makefile->close();

exit(0);
