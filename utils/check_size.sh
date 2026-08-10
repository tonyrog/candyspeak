# list function and size in an elf file
#
ELFFILE=$1
NM=nm

$NM --size-sort -S -C $1 | \
    awk '$3=="T"||$3=="t"{n=strtonum("0x"$2); t+=n; printf "%6d  %s\n", n, $4}' | \
    sort -rn | \
    head -100
$NM --size-sort -S -C $1 | \
    awk '$3=="T"||$3=="t"{n=strtonum("0x"$2); t+=n; \
    	if ($4 ~ /^(csp_|setup_|eval[0-9]|est_|fn_|add_|op_info|tok_table|decl_#table|build_dis_ip|can_mark_fields|main$)/) ours+=n; \
	    else core+=n } \
	    END {printf "\n%6d  CandySpeak (main = setup+loop, inlined)\n%6d  core/libc/USB\n%6d  TOTAL text\n", ours, core, t}'
