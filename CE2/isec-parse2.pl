#!/usr/bin/perl -w

# /tmp/_9/0000.vcf        for records private to  giab-filt-norm.vcf.gz
# /tmp/_9/0001.vcf        for records private to  NA12878_V2.5_Robot_2.srt.bqsr.crumble-9.cram.norm.vcf.gz
# /tmp/_9/0002.vcf        for records from giab-filt-norm.vcf.gz shared by both   giab-filt-norm.vcf.gz NA12878_V2.5_Robot_2.srt.bqsr.crumble-9.cram.norm.vcf.gz
# /tmp/_9/0003.vcf        for records from NA12878_V2.5_Robot_2.srt.bqsr.crumble-9.cram.norm.vcf.gz shared by both  giab-filt-norm.vcf.gz NA12878_V2.5_Robot_2.srt.bqsr.crumble-9.cram.norm.vcf.gz

my $dir=shift(@ARGV);

my @i;
my @i20;
my @s;
my @s20;
my %counts;

# Parse based on $F[3] lengths not matching $F[4] lengths
for (my $x=0;$x<=3;$x++) {
    open(FH, "<$dir/000$x.vcf") || die;
    while (<FH>) {
	last unless /^#/;
    }
    # misses 1st rec, but sufficiently close!
    while (<FH>) {
	my @F = split("\t", $_);

	# Differing lengths between ref ($F[3]) and any of the sample ($F[4]) alts
	# implies an indel is present.
	my $l = 0;
	$l++ if $F[3] =~ /[^,][^,]/;
	$l++ if $F[4] =~ /[^,][^,]/;

	$counts{$F[6]}++;

	if ($l) {
	    $i[$x]{$F[6]}++;
	    $i20[$x]{$F[6]}++ if ($F[5] > 20);
	} else {
	    $s[$x]{$F[6]}++;
	    $s20[$x]{$F[6]}++ if ($F[5] > 20);
	}
    }
    close(FH);
    # foreach (keys $s[$x]) { print "SET s[$x]{$_} = $s[$x]{$_}\n"; }
}

# Precision = true pos / (true pos + false pos) = [3] / ([3] + [1])
# Recall    = true pos / (true pos + false neg) = [3] / ([3] + [0])
my ($p,$r);

my @rank = qw/PASS VQSRTrancheSNP90.00to99.00 VQSRTrancheSNP99.00to99.90 VQSRTrancheSNP99.90to100.00 LowQual/;
for (my $i = 0; $i <= 4; $i++) {
    my $s1=0;
    my $s3=0;
    print "$rank[$i] and above: $counts{$rank[$i]} calls\n";
    for (my $j=0; $j <= $i; $j++) {
	#print "XYZZY $j $rank[$j] $s[1]{$rank[$j]}\n";
	$s1 += $s[1]{$rank[$j]};
	$s3 += $s[3]{$rank[$j]};
    }
    $p=100.0*$s3/($s3+$s1+1e-10);
    $r=100.0*$s3/($s3+$s[0]{PASS}+1e-10);
    printf("  SNP     precision = %6.8f%%\n",$p);
    printf("  SNP     recall    = %6.8f%%\n",$r);
    printf("  SNP     F-score   = %6.8f%%\n\n",2*$p*$r/($p+$r+1e-10));
}

exit 0;

print "=" x 70, "\n\n";

for (my $i = 0; $i <= 4; $i++) {
    my $s1=0;
    my $s3=0;
    print "$rank[$i] and above:\n";
    for (my $j=0; $j <= $i; $j++) {
	$s1 += $s20[1]{$rank[$j]};
	$s3 += $s20[3]{$rank[$j]};
    }
    $p=100.0*$s3/($s3+$s1+1e-10);
    $r=100.0*$s3/($s3+$s20[0]{PASS}+1e-10);
    printf("  SNP20   precision = %6.8f%%\n",$p);
    printf("  SNP20   recall    = %6.8f%%\n",$r);
    printf("  SNP20   F-score   = %6.8f%%\n\n",2*$p*$r/($p+$r+1e-10));
}

