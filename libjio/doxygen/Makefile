
ifneq ($(V), 1)
        NICE_DOXYGEN = @echo "  DOXYGEN  $@"; doxygen
else
        NICE_DOXYGEN = doxygen
endif


default: all

all: public internal

public:
	$(NICE_DOXYGEN) Doxyfile.public

internal:
	$(NICE_DOXYGEN) Doxyfile.internal

clean:
	rm -rf doc.internal doc.public


.PHONY: all clean default doxygen internal public

