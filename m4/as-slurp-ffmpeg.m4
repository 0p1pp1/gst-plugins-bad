dnl slurp-ffmpeg.m4 0.1.1
dnl a macro to slurp in ffmpeg's cvs source inside a project tree
dnl taken from Autostar Sandbox, http://autostars.sourceforge.net/

dnl Usage:
dnl AS_SLURP_FFMPEG(DIRECTORY, DATE, [ACTION-IF-WORKED [, ACTION-IF-NOT-WORKED]]])
dnl
dnl Example:
dnl AM_PATH_FFMPEG(lib/ffmpeg, 2002-12-14 12:00 GMT)
dnl
dnl make sure you have a Tag file in the dir where you check out that
dnl is the Tag of CVS you want to have checked out
dnl it should correspond to the DATE argument you supply, ie resolve to
dnl the same date
dnl (in an ideal world, cvs would understand it's own Tag file format as
dnl a date spec)

AC_DEFUN(AS_SLURP_FFMPEG,
[
  # save original dir
  DIRECTORY=`pwd`
  # get/update cvs
  if test ! -d $1; then mkdir -p $1; fi
  cd $1

  if test ! -d ffmpeg/CVS; then
    # check out cvs code
    AC_MSG_NOTICE(checking out ffmpeg cvs code from $2 into $1)
    cvs -Q -d:pserver:anonymous@cvs.ffmpeg.sourceforge.net:/cvsroot/ffmpeg co -D '$2' ffmpeg || FAILED=yes
    cd ffmpeg
  else
    # compare against Tag file and see if it needs updating
    if diff -q Tag ffmpeg/CVS/Tag > /dev/null 2> /dev/null
    then
      # diff returned no problem
      AC_MSG_NOTICE(ffmpeg cvs code in sync)
    else
      # diff says they differ
      cd ffmpeg 
      AC_MSG_NOTICE(updating ffmpeg cvs code)
      cvs -Q update -dP -D '$2' || FAILED=yes
    fi
  fi
  
  # now go back
  cd $DIRECTORY

  if test "x$FAILED" == "xyes"; then
    [$4]
    false
  else
    [$3]
    true
  fi
])
