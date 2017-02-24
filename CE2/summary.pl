#!/usr/bin/perl

# collates the output files from compare.sh.
#
# Assumes the VCF file given is the 90.00 tranch one, which sets PASS
# for 90.00 to 99.00 and various VQSR labels for the others. This
# permits us to compute all tranches from a single file.

my %sample;
my %counts;
my %size;

foreach my $fn (@ARGV) {
    $fn =~ /(.*recal)\.(\d+)(\S+)bam/;
    my ($base,$chr,$method) = ($1, $2, $3);
    #print "base $base, chr $chr, method $method\n";

    if (-e "$base.$chr${method}bam.cram.size") {
	open(SZ, "<$base.$chr${method}bam.cram.size");
	while (<SZ>) {
	    if (/QS$/) {
		my @F=split(/\s+/,$_);
		$size{$base}{$chr}{$method}=$F[5];
	    }
	}
    }

    open(my $fh, "<$fn") || die;
    my $tranch = "";
    while (<$fh>) {
	if (/^(\S+)\sand above: (\d+)/) {
	    $tranch = $1;
	    $counts{$base}{$chr}{$tranch}=$2;
	    next;
	}
	if (/SNP\s+(\S+)\s+= ([\d.]+)/) {
	    $sample{$base}{$chr}{$method}{$tranch}{$1}=$2;
	    next;
	}
    }
    close($fh);
}

foreach my $base (keys %sample) {
    foreach my $chr (keys %{$sample{$base}}) {
	print "\n=== Sample $base, chr $chr ===\n";
	# For SPF trios theta90, 99, 99.9 and 100.
	# method size size S P F [S P F]...
	my $t900  = $counts{$base}{$chr}{"PASS"};
	my $t990  = $counts{$base}{$chr}{"VQSRTrancheSNP90.00to99.00"} + $t900;
	my $t999  = $counts{$base}{$chr}{"VQSRTrancheSNP99.00to99.90"} + $t990;
	my $t1000 = $counts{$base}{$chr}{"VQSRTrancheSNP99.90to100.00"} + $t999;
	printf("%-12s | %10s | %-10s / %6d | %-10s / %6d | %-10s / %6d | %-10s / %6d\n",
	       "METHOD", "SIZE",
	       "theta=90.0", $t900,
	       "theta=99.0", $t990,
	       "theta=99.9", $t999,
	       "theta=100.0",$t1000);
	foreach my $method (sort keys %{$sample{$base}{$chr}}) {
	    my $size = exists($size{$base}{$chr}{$method}) ? $size{$base}{$chr}{$method} : 0;
	    my $m = $method;
	    $m =~ s/\.//g;
	    $m = "lossless" if $m eq "";
	    printf("%-12s | %10d ", $m, $size);
	    foreach my $tranch (qw/PASS VQSRTrancheSNP90.00to99.00 VQSRTrancheSNP99.00to99.90 VQSRTrancheSNP99.90to100.00/) {
		printf("|%6.2f ",$sample{$base}{$chr}{$method}{$tranch}{"recall"});
		printf("%6.2f ",$sample{$base}{$chr}{$method}{$tranch}{"precision"});
		printf("%6.2f ",$sample{$base}{$chr}{$method}{$tranch}{"F-score"});
	    }
	    print "\n";
	}
    }
}
