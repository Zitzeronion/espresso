# Copyright (C) 2010,2011,2012,2013,2014 The ESPResSo project
# Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009,2010 
#   Max-Planck-Institute for Polymer Research, Theory Group
#  
# This file is part of ESPResSo.
#  
# ESPResSo is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#  
# ESPResSo is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#  
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>. 
# 
source "tests_common.tcl"
puts "-------------------------------------------"
puts "Testcase h5mdfile.tcl"
puts "-------------------------------------------"



# #CREATE NEW DATASET AND WRITE VALUES
# h5mdfile H5Fcreate "h5mdfile.h5" 
# 
# h5mdfile H5Gcreate2 "group1"
# h5mdfile H5Screate_simple type int dims 10 5 3
# h5mdfile H5Pset_chunk dims 2 2 2
# h5mdfile H5Dcreate2 "/group1/dset"
# h5mdfile H5_write_value value 100 index 8 3 2
# set E [expr [h5mdfile H5_read_value index 8 3 2]]
# puts $E
# h5mdfile H5Dwrite
# 
# h5mdfile H5Gcreate2 "group2"
# h5mdfile H5Screate_simple type int dims 15 5 3
# h5mdfile H5Pset_chunk dims 2 2 2
# h5mdfile H5Dcreate2 "/group2/dset"
# h5mdfile H5_write_value value 200 index 13 3 2
# set E [expr [h5mdfile H5_read_value index 13 3 2]]
# puts $E
# h5mdfile H5Dwrite
# 
# h5mdfile H5Pclose
# h5mdfile H5Dclose
# h5mdfile H5Sclose
# h5mdfile H5Gclose
# h5mdfile H5Fclose




# #WRITE TO EXISITNG DATASET
# h5mdfile H5Fopen "h5mdfile.h5"
# h5mdfile H5Gopen2 "/group1"
# h5mdfile H5Dopen2 "/group1/dset"
# h5mdfile H5Dread
# h5mdfile H5_write_value value 111 index 7 2
# set E [expr [h5mdfile H5_read_value index 7 2]]
# puts $E
# 
# h5mdfile H5Gopen2 "/group2"
# h5mdfile H5Dopen2 "/group2/dset"
# h5mdfile H5Dread
# h5mdfile H5_write_value value 222 index 5 3
# set E [expr [h5mdfile H5_read_value index 5 3]]
# puts $E
# 
# h5mdfile H5Dclose
# h5mdfile H5Gclose
# h5mdfile H5Fclose



#EXTEND DATASET
h5mdfile H5Fcreate "h5mdfile.h5" 
h5mdfile H5Gcreate2 "group1"
h5mdfile H5Screate_simple type int dims 10 3
h5mdfile H5Pset_chunk dims 2 5
h5mdfile H5Dcreate2 "/group1/dset"
h5mdfile H5_write_value value 111 index 5 1
set E [expr [h5mdfile H5_read_value index 5 1]]
puts $E
h5mdfile H5Dwrite

h5mdfile H5Dextend int dims 20 3
h5mdfile H5Sselect_hyperslab offset 10 0
h5mdfile H5Screate_simple type int dims 10 3
h5mdfile H5_write_value value 222 index 5 1	
set E [expr [h5mdfile H5_read_value index 5 1]]
puts $E
h5mdfile H5Dwrite

h5mdfile H5Dextend int dims 30 3
h5mdfile H5Sselect_hyperslab offset 20 0
h5mdfile H5Screate_simple type int dims 10 3
h5mdfile H5_write_value value 333 index 5 1	
set E [expr [h5mdfile H5_read_value index 5 1]]
puts $E
h5mdfile H5Dwrite

h5mdfile H5Pclose
h5mdfile H5Dclose
h5mdfile H5Sclose
h5mdfile H5Gclose
h5mdfile H5Fclose

exit 0
