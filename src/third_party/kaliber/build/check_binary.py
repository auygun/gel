import sys
import shutil

if shutil.which(sys.argv[1]):
  print("1")
else:
  print("0")
