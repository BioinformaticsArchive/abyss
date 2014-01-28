#!/usr/bin/make -rRf

#------------------------------------------------------------
# optional params
#------------------------------------------------------------

# Number of levels in counting bloom filter.
# Note: Changing this value will currently break the script.
l:=2

#------------------------------------------------------------
# global vars
#------------------------------------------------------------

PROGRAM_NAME=abyss-bloom-dist.mk

# a single space character
space:=$(noop) $(noop)
b_times_l:=$(shell echo '$b * $l' | bc)
b_mod_w:=$(shell echo '$b % $w' | bc)
windows:=$(shell seq 1 $w)
l1_bloom_files:=\
	$(foreach window,$(windows),\
		$(foreach file,$(files),\
			$(name)_l1_w$(window)_$(file).bloom.gz))
l2_bloom_files:=\
	$(foreach window,$(windows),\
		$(foreach file,$(files),\
			$(name)_l2_w$(window)_$(file).bloom.gz))

#------------------------------------------------------------
# functions
#------------------------------------------------------------

# arg 1: filename of the form $(name)_l<level>_w<window>_<readfile>.bloom
# output: list of filename parts, split by '_'
splitBloomFilename=$(subst _, ,$(subst .bloom.gz,,$1))

# arg 1: filename of the form $(name)_l<level>_w<window>_<readfile>.bloom
# output: <window>
getWindow=$(subst w,,$(word 3,$(call splitBloomFilename,$1)))

# arg 1: filename of the form $(name)_l<level>_w<window>_<readfile>.bloom
# output: <readfile>
getReadFilename=$(subst $(space),_,$(wordlist 4,999,$(call splitBloomFilename,$1)))

# arg 1: filename of the form $(name)_l1_w<window>_<readfile>.bloom
# output: list of bloom filter files for level 1, window <window>, excluding
#         input (arg 1).
getSiblingBloomFiles=$(filter-out $1,$(filter $(name)_l1_w$(call getWindow,$1)_%.bloom.gz,$(l1_bloom_files)))

#------------------------------------------------------------
# special rules
#------------------------------------------------------------

.PHONY: args_check build
default: build

#------------------------------------------------------------
# parameter checking rule
#------------------------------------------------------------

args_check:
ifndef name
	$(error $(PROGRAM_NAME): missing required parameter 'name')
endif
ifndef k
	$(error $(PROGRAM_NAME): missing required parameter 'k')
endif
ifndef b
	$(error $(PROGRAM_NAME): missing required parameter 'b')
endif
ifndef w
	$(error $(PROGRAM_NAME): missing required parameter 'w')
endif
ifndef files
	$(error $(PROGRAM_NAME): missing required parameter 'files')
endif
ifneq ($(b_mod_w), 0)
	$(error $(PROGRAM_NAME): `b' ($b) must be divisible by `w' ($w))
endif

#------------------------------------------------------------
# main rules
#------------------------------------------------------------

build: args_check $(name).bloom.gz

# level 1 bloom filter files
$(name)_l1_%.bloom.gz: $(files)
	abyss-bloom build -v -k$k -b$b -w$(call getWindow,$@)/$w \
		- $(call getReadFilename,$@) | gzip -c > $@

# level 2 bloom filter files
$(name)_l2_%.bloom.gz: $(l1_bloom_files)
	zcat $(call getSiblingBloomFiles,$(subst l2,l1,$@)) | \
		abyss-bloom build -v -k$k -b$(b_times_l) -l2 \
		-w$(call getWindow,$@)/$w \
		$(foreach i,$(wordlist 2,999,$(files)),-L1=-) \
		- $(call getReadFilename,$@) | \
			gzip -c > $@

# final output file
$(name).bloom.gz: $(l2_bloom_files)
	zcat $(l2_bloom_files) | \
		abyss-bloom union -v -k$k - \
		$(foreach i,$(l2_bloom_files),-) | \
			gzip -c > $@

#------------------------------------------------------------
# debugging rules
#------------------------------------------------------------

debug:
	@echo 'b_times_l=$(b_times_l)'
	@echo 'b_mod_w=$(b_mod_w)'
	@echo 'l1_bloom_files="$(l1_bloom_files)"'
	@echo 'l2_bloom_files="$(l2_bloom_files)"'
	@echo '$$(call getWindow, $(word 1,$(l1_bloom_files))): $(call getWindow,$(word 1,$(l1_bloom_files)))'
	@echo '$$(call getReadFilename, $(word 1,$(l1_bloom_files))): $(call getReadFilename,$(word 1,$(l1_bloom_files)))'
	@echo '$$(call getSiblingBloomFiles, $(word 1,$(l1_bloom_files))): $(call getSiblingBloomFiles,$(word 1,$(l1_bloom_files)))'
