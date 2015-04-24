#!/usr/bin/make -Rrf

SHELL=/bin/bash -o pipefail

#------------------------------------------------------------
# simulated test data
#------------------------------------------------------------

# yeast chromosome III, 50X coverage
ref_url?='http://www.ebi.ac.uk/ena/data/view/X59720&display=fasta'
ref:=ref.fa
read_len:=100
cov:=50
num_read_pairs:=316600
test_read1:=read1.fq
test_read2:=read2.fq

#------------------------------------------------------------
# assembly outputs
#------------------------------------------------------------

standard_assembly_dir?=standard-assembly
dida_assembly_dir?=dida-assembly
assembly_name?=test

#------------------------------------------------------------
# assembly params
#------------------------------------------------------------

k?=30
in?=../$(test_read1) ../$(test_read2)
abyss_opt=v=-v k=$k name='$(assembly_name)' in='$(in)'

#------------------------------------------------------------
# meta rules
#------------------------------------------------------------

default: $(dida_assembly_dir)/stats-8 $(standard_assembly_dir)/stats-8

#------------------------------------------------------------
# rules for downloading data
#------------------------------------------------------------

$(ref):
	curl $(ref_url) > $@

$(test_read1) $(test_read2): $(ref)
	wgsim -N $(num_read_pairs) -1 $(read_len) -2 $(read_len) \
		$^ $(test_read1) $(test_read2)

#------------------------------------------------------------
# rules for running assemblies
#------------------------------------------------------------

$(standard_assembly_dir):
	mkdir -p $@

$(dida_assembly_dir):
	mkdir -p $@

$(standard_assembly_dir)/$(assembly_name)-8.fa: $(test_read1) $(test_read2) \
		| $(standard_assembly_dir)
	abyss-pe -C $(standard_assembly_dir) $(abyss_opt) \
		2>&1 | tee $(standard_assembly_dir)/log

$(dida_assembly_dir)/$(assembly_name)-8.fa: $(test_read1) $(test_read2) \
		| $(dida_assembly_dir)
	abyss-pe -C $(dida_assembly_dir) $(abyss_opt) aligner=dida-wrapper \
		2>&1 | tee $(dida_assembly_dir)/log

%/stats-8: %/$(assembly_name)-8.fa
	abyss-fac $^ | tee $@
