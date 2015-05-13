#!/usr/bin/perl
# this is a good question.
# is it something that you write in perl?
# yes because you have better tools

# so what do I want to do with this in perl?

# need to make the variable names (which are extern)
# need to make the array that uses the function

# for every benchmark type make an extern variable
# for 

my @benchmarks;

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

foreach(@benchmarks) {
	print "extern Bench ", $_, ";";
}

print qq(

Bench benches[] = {
);
foreach(@benchmarks) {
	print "\t",$_,","
}
print qq(
};
)