#!/usr/bin/perl
use strict;
use autodie;

my @benchmarks;
my @benches;

open(BENCHFILE, "./benchtypes");

while (<BENCHFILE>) {
	chomp($_);
	@benches = split(/\s+/, $_);
	# need input validation
	push(@benchmarks, \@benches);
}

for my $bench (@benchmarks) {
	print "val: ", @$_, @$_[0], "\n";
	print "extern Bench ", @$_[0], ";";
}

# I need to apply the entire thing to the join.
#my $benchfiles = join " ", map { $_,".c"} @benchmarks;
#my $cfiles = "compbench.c",$benchfiles;
#my $hfiles = "benches.h compbench.h";
#my $files = $cfiles, $hfiles;
## I'm going to need to know about how to do the mpi stuff
## I need to know which it is. 
## what I'm really making here is a struct. I need to go through these and make it behave.
#print qq(
#compbench: $files
#	\$CC 
#)
