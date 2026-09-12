# list function and size in an elf file
#
#   utils/check_size.sh <elf>          # NM=avr-nm utils/check_size.sh ... for AVR
#
# The second bucket is EVERYTHING THAT IS NOT CandySpeak -- libgcc's arithmetic
# helpers, the port's interrupt vectors, string literals, the ROM image. It used
# to be labelled "USB", which was true when the boards under test were a 32U4
# and the SAMD parts: there the USB stack really is ~2.9K plus a 4K bootloader.
# On an ATmega328P there is no USB hardware at all, and the label sent someone
# looking for a stack that does not exist. Named for what it is now.
ELFFILE=$1
# avr-nm when this is an AVR image: the host nm can often read one, but it is
# not the tool that knows the format. NM= overrides.
NM=${NM:-nm}

$NM --size-sort -S -C $1 | \
    awk '$3=="T"||$3=="t"{n=strtonum("0x"$2); t+=n; printf "%6d  %s\n", n, $4}' | \
    sort -rn | \
    head -100
$NM --size-sort -S -C $1 | \
    awk '$3=="T"||$3=="t"{n=strtonum("0x"$2); t+=n; \
    	if ($4 ~ /^(csp_|setup_|eval[0-9]|est_|fn_|add_|op_info|tok_table|decl_table|build_dis_ip|buf_mark_fields|main$)/) ours+=n; \
	    else core+=n } \
	    END {printf "\n%6d  CandySpeak (main = setup+loop, inlined)\n%6d  everything else (libgcc, vectors, literals, the ROM image)\n%6d  TOTAL text\n", ours, core, t}'
