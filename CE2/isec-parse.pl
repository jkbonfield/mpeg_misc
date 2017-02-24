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
my $count = 0;

# Parse based on $F[3] lengths not matching $F[4] lengths
for (my $x=0;$x<=3;$x++) {
    open(FH, "<$dir/000$x.vcf") || die;
    my $indels=0;
    my $snps=0;
    my $indels20 = 0;
    my $snps20 = 0;
    while (<FH>) {
	last unless /^#/;
    }
    # misses 1st rec, but sufficiently close!
    while (<FH>) {
	my @F = split("\t", $_);

	next unless $F[6] eq "PASS";
	$count++;

	# Differing lengths between ref ($F[3]) and any of the sample ($F[4]) alts
	# implies an indel is present.
	my $x = 0;
	$x++ if $F[3] =~ /[^,][^,]/;
	$x++ if $F[4] =~ /[^,][^,]/;

	if ($x) {
	    $indels++;
	    $indels20++ if ($F[5] > 20);
	} else {
	    $snps++;
	    $snps20++ if ($F[5] > 20);
	}
    }
    close(FH);
    $i[$x]=$indels;
    $i20[$x]=$indels20;
    $s[$x]=$snps;
    $s20[$x]=$snps20;

    #print "$x: $indels\t$indels20\t\t$snps\t$snps20\n";
}
#print "\n";


print "Processed $count SNPs/Indels\n";

# Precision = true pos / (true pos + false pos) = [3] / ([3] + [1])
# Recall    = true pos / (true pos + false neg) = [3] / ([3] + [0])
my ($p,$r);

$p=100.0*$s[3]/($s[3]+$s[1]+1e-10);
$r=100.0*$s[3]/($s[3]+$s[0]+1e-10);
printf("SNP     precision = %6.8f%%\n",$p);
printf("SNP     recall    = %6.8f%%\n",$r);
printf("SNP     F-score   = %6.8f%%\n\n",2*$p*$r/($p+$r+1e-10));

$p=100.0*$i[3]/($i[3]+$i[1]+1e-10);
$r=100.0*$i[3]/($i[3]+$i[0]+1e-10);
if ($p+$r > 0) {
    printf("Indel   precision = %6.8f%%\n",$p);
    printf("Indel   recall    = %6.8f%%\n",$r);
    printf("Indel   F-score   = %6.8f%%\n\n",2*$p*$r/($p+$r+1e-10));
}

$p=100.0*$s20[3]/($s20[3]+$s20[1]+1e-10);
$r=100.0*$s20[3]/($s20[3]+$s20[0]+1e-10);
printf("SNP20   precision = %6.8f%%\n",$p);
printf("SNP20   recall    = %6.8f%%\n",$r);
printf("SNP20   F-score   = %6.8f%%\n\n",2*$p*$r/($p+$r+1e-10));

$p=100.0*$i20[3]/($i20[3]+$i20[1]+1e-10);
$r=100.0*$i20[3]/($i20[3]+$i20[0]+1e-10);
if ($p+$r > 0) {
    printf("Indel20 precision = %6.8f%%\n",$p);
    printf("Indel20 recall    = %6.8f%%\n",$r);
    printf("Indel20 F-score   = %6.8f%%\n\n",2*$p*$r/($p+$r+1e-10));
}

