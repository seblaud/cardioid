# Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at the
# Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights reserved.
# See file COPYRIGHT for details.
#
# This file is part of the MFEM library. For more information and source code
# availability see http://mfem.org.
#
# MFEM is free software; you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License (as published by the Free
# Software Foundation) version 2.1 dated February 1999.

# Use the MFEM build directory
MFEM_DIR = ..
CONFIG_MK = $(MFEM_DIR)/config/config.mk
TEST_MK = $(MFEM_DIR)/config/test.mk
# Use the MFEM install directory
# MFEM_DIR = ../mfem
# CONFIG_MK = $(MFEM_DIR)/config.mk

MFEM_LIB_FILE = mfem_is_not_built
-include $(CONFIG_MK)

SEQ_EXAMPLES = fiber 
#SEQ_EXAMPLES = mfemTest
PAR_EXAMPLES = fiberp

SOURCE = io.cpp fiber.cpp solver.cpp utils.cpp genfiber.cpp cardfiber.cpp triplet.cpp
#OBJECT = io.o  fiber.o
OBJECT = $(SOURCE:.cpp=.o)

ifeq ($(MFEM_USE_MPI),NO)
   EXAMPLES = $(SEQ_EXAMPLES)
else
   EXAMPLES = $(PAR_EXAMPLES) $(SEQ_EXAMPLES)
endif

.PHONY: all clean clean-build clean-exec

all: $(SOURCE) $(EXAMPLES)
	
$(EXAMPLES): $(OBJECT)
	$(MFEM_CXX) $(MFEM_FLAGS) $(OBJECT) -o $@ $(MFEM_LIBS)
	
.cpp.o:
	$(MFEM_CXX) $(MFEM_FLAGS) -c $(<) -o $(@)


test: mfemTest kdtreeTest

mfemTest:
	$(MFEM_CXX) $(MFEM_FLAGS) -c mfemTest.cpp -o mfemTest.o
	$(MFEM_CXX) $(MFEM_FLAGS) mfemTest.o -o $@ $(MFEM_LIBS)
	./mfemTest
	
kdtreeTest:
	$(MFEM_CXX) $(MFEM_FLAGS) kdtreeTest.cpp -o $@ 
	./kdtreeTest


# Testing: "test" target and mfem-test* variables are defined in config/test.mk

# Generate an error message if the MFEM library is not built and exit
$(MFEM_LIB_FILE):
	$(error The MFEM library is not built)

clean: clean-build clean-exec clean-test

clean-build:
	rm -f *.o *~ $(SEQ_EXAMPLES) $(PAR_EXAMPLES)
	rm -rf *.dSYM *.TVD.*breakpoints

clean-exec:
	@rm -f refined.mesh displaced.mesh mesh.* ex5.mesh
	@rm -rf Example5* Example9* Example15*
	@rm -f sphere_refined.* sol.* sol_u.* sol_p.*
	@rm -f ex9.mesh ex9-mesh.* ex9-init.* ex9-final.*
	@rm -f deformed.* velocity.* elastic_energy.* mode_*

clean-test:
	rm -rf mfemTest.o mfemTest kdtreeTest.o kdtreeTest