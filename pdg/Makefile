ODIR = ./build
output_folder := $(shell mkdir -p $(ODIR))

all: libplugin.so

libplugin.so:
	@echo Configuring...
	@cd $(ODIR) && cmake ..
	@echo Building...
	@$(MAKE) -C $(ODIR)

clean:
	@echo Cleaning up...
	@rm -rf $(ODIR)

