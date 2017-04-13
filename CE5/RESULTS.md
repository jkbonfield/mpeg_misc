First 10k names

              gzip   bzip2  xz     fqz-n1  fqz-n2  lpaq   paq8o9 cmix   tok   tok2  tok3
    01.names  71924  59055  34412  23178   20447   26770  17979  17131  18005 17973 17863   +4.3% 
    02.names  75554  62650  63532  54438   46951   47319  43753  42274  43936 43684 43654   +3.3%
    03.names  93466  72606  78100  124050  153406  54506  48200  46086* 70824 52939 52768  +14.5%%
    05.names  52906  49121  44316  54652   50104   32750  30953  29990  30484 30993 31008   +3.4%
    08_names  94418  73189  79408  61101   43944   50236  43987  42314  43778 43685 43698   +3.3%
    09.names  53401  39669  38536  30330   41707   28872  26544  25944  31334 26777 26801   +3.3%
    10.names  72630  54588  55452  55757   46711   40435  38085  36981  37893 37833 37796   +2.2%
    20.names  54038  45386  29508  18939   14697   18447  15444  14706  14711 14690 14610   -0.7%

\* denotes significant improvements compared to tok/tok2. Note however
these tools are vastly slower (especially cmix).

"tok" figures are from tokenise_name.c code using the old shell scripts for packing.

"tok2" is with the combined entropy encoder/decoder and packing (../comp/codec.c).

"tok3" is based on tokenise_name2 but with inbuilt codec to output a
single stream.
