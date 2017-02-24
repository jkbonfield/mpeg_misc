#!/bin/sh

# ASSUMPTION on filename structure
samp=$1
chr=`echo $samp | sed 's/.*recal\.\([0-9]*\)\..*/\1/'`
echo chr=$chr

ref=GATK_bundle/human_g1k_v37.fasta
hc=NA12878_GIAB_highconf_IllFB-IllGATKHC-CG-Ion-Solid_ALLCHROM_v3.2.2_highconf
bed=$hc.$chr.bed.gz

# Already nromalised gold standard
gold_vcf=$hc.${chr}nf.vcf.gz
if [ ! -e $gold_vcf ]
then
    echo "Normalising & filtering gold standard"
    bcftools norm -f $ref -T $bed $hc.$chr.vcf.gz | bgzip -c > $hc.${chr}nf.vcf.gz
    bcftools index $hc.${chr}nf.vcf.gz
fi

# Normalise our input sample
bcftools norm -f $ref -T $bed $samp | bgzip -c > $samp.TMP.vcf.gz
bcftools index $samp.TMP.vcf.gz
bcftools isec -p $samp.TMP $gold_vcf $samp.TMP.vcf.gz
#/nfs/users/nfs_j/jkb/work/mpeg/isec-parse.pl $samp.TMP
/nfs/users/nfs_j/jkb/work/mpeg/isec-parse2.pl $samp.TMP
rm -rf $samp.TMP*
