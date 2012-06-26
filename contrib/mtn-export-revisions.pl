#! /usr/bin/env perl
use strict;
use warnings;

# mtn-export-revisions.pl - exports revisions into montone packet format
#
# Reads revision IDs from STDIN and exports any fdata, fdelta, rdata,
# pubkey, and cert packets ready to be fed to "mtn read"; for example on
# a different machine or database. The revision IDs from STDIN can be in
# any order as they are automatically toposorted.
#
# Alternatively, you can pass selectors on the command line.
#
# BUGS:
# This script was bodged, but it should instead
#   + be written by someone who actually knows Perl
#   + use Tony's excellent Monotone::AutomateStdio Perl library
# 
use List::MoreUtils 'uniq';

# export_revision($revision_id)
sub export_revision {
    my $id = shift;
    print STDERR "Exporting revision: $id\n";
    my $revision = `mtn au get_revision $id`;
    print STDERR $revision;

    print STDERR "Files\n";
    my @files = $revision =~ /add_file.*?\n content \[([a-f0-9]{40})\]\n/gs;
    foreach (@files) {
        print STDERR "  file '$_'\n";
        print `mtn au packet_for_fdata $_`;
    }

    print STDERR "Patches\n";
    my %patches = $revision =~
      /patch.*?\n from \[([a-f0-9]{40})\]\n   to \[([a-f0-9]{40})\]/gs;
    foreach ( keys %patches ) {
        print STDERR "  patch from '$_' to '$patches{$_}'\n";
        print `mtn au packet_for_fdelta $_ $patches{$_}`;
    }

    print STDERR "Public keys\n";
    my $certs_output = `mtn au certs $id`;
    my @pubkeys = $certs_output =~ /      key \[([a-f0-9]{40})\]/;
    foreach ( uniq(@pubkeys) ) {
        print STDERR "  pubkey '$_'\n";
        print `mtn au get_public_key $_`;
    }

    print `mtn au packet_for_rdata $id`;
    print `mtn au packets_for_certs $id`;
}

sub get_revisions {
    my @revisions;

    if ( $#ARGV == -1 ) {
        while ( my $id = <STDIN> ) {
            chomp $id;
            push( @revisions, $id );
        }
    }
    else {
        foreach my $selector (@ARGV) {
            my @selected = split( "\n", `mtn au select $selector` );
            foreach my $id (@selected) {
                push( @revisions, $id );
            }
        }
    }

    return join( ' ', @revisions );
}

my $revisions = get_revisions;
my @toposorted = split( "\n", `mtn au toposort $revisions` );

foreach my $id (@toposorted) {
    export_revision($id);
}

exit(0);
