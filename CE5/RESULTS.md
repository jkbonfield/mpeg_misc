First 10k names

              gzip   bzip2  xz     fqz-n1  fqz-n2  lpaq   paq8o9 cmix   tok   tok2  tok3
    01.names  71924  59055  34412  23178   20447   26770  17979  17131  18005 17973 17774  +3.8%
    02.names  75554  62650  63532  54438   46951   47319  43753  42274  43936 43684 43560  +3.0%
    03.names  93466  72606  78100  124050  153406  54506  48200  46086* 70824 52939 50586  +9.8%
    05.names  52906  49121  44316  54652   50104   32750  30953  29990  30484 30993 30931  +3.1%
    08_names  94418  73189  79408  61101   43944   50236  43987  42314  43778 43685 43537  +2.9%
    09.names  53401  39669  38536  30330   41707   28872  26544  25944  31334 26777 26788  +3.3%
    10.names  72630  54588  55452  55757   46711   40435  38085  36981  37893 37833 37728  +2.0%
    20.names  54038  45386  29508  18939   14697   18447  15444  14706  14711 14690 14512  -1.3%

\* denotes significant improvements compared to tok/tok2. Note however
these tools are vastly slower (especially cmix).

"tok" figures are from tokenise_name.c code using the old shell scripts for packing.

"tok2" is with the combined entropy encoder/decoder and packing (../comp/codec.c).

"tok3" is based on tokenise_name2 but with inbuilt codec to output a
single stream.  It also deduplicates streams (key on 03.names).

../../CE5/01.names 17774
../../CE5/02.names 43560
../../CE5/03.names 50586
../../CE5/05.names 30931
../../CE5/07.names 17774
../../CE5/08.names 43537
../../CE5/09.names 26788
../../CE5/10.names 37728
../../CE5/ON.names 4288
../../CE5/on.names 228517
../../CE5/20.1.names 14512

Still gzipping these results gives significant savings on on.names
(6.8%), ON.names (3.7%) and 03.names (1.2%).  So there is some
duplication still.

Missing correlations on 03.names, sorted data example:

m130618_233855_00127_c100506252550000001823078908081383_s1_p0/137460/0_115
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/137460/158_1995
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/137460/158_1995
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/137460/2039_4119
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/137460/4169_5446
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/137460/4169_5446
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/14283/0_7507
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/148812/0_8310
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/148812/10816_13207
m130618_233855_00127_c100506252550000001823078908081383_s1_p0/148812/15641_17345

"/4169_5446" x2, "/158_1995" x2 shows correlation between last two
integer fields, but typically this is an exact match so we already
capture this?

"p0/137460/" x6 shows this value isn't entirely random but has
repeated values, implying maybe we need to search back further when
computing our name to compare against.

The "/137460/0_5446" integers comprise most file storage along with
column 0 DIFF vs DUP back refs.
