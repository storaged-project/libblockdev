API_FILES := $(wildcard *.api)
SOURCE_FILES := $(patsubst %.api,%.c,${API_FILES})
HEADER_FILES := $(patsubst %.api,%.h,${API_FILES})

all-local: generate_boilerplate

%.c %.h: %.api
	${srcdir}/../../../scripts/boilerplate_generator.py $*.api ./

generate_boilerplate: ${SOURCE_FILES} ${HEADER_FILES}

dist_noinst_HEADERS = ${SOURCE_FILES} ${HEADER_FILES}

MAINTAINERCLEANFILES = Makefile.in

.PHONY: generate_boilerplate
