###############################################################################
############################  Basic Use-Case LIBRARY   ###########################
###############################################################################
O_TARGETS += baseuc
EXOBJ :=  lmusdk.o base_uc.o
OBJ := hello.o
INC := $(TOPINC)
DEF := $(TOPDEF)

base_uc.o: $(OBJ)
	@(echo "(LD)  $@ <= $^")
	@($(LD) -r -o $@ -Map $*.map $^)

baseuc.o: $(EXOBJ)
	@(echo "(LD)  $@ <= $^")
	@($(LD) -r -o $@ -Map $*.map $^)

include $(TOPDIR)/Rules.make