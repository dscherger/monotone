#!/bin/sh
#
# File: mtbrowse.sh
# Description: Text based browser for monotone source control.
# url: http://www.venge.net/monotone/
# Licence: GPL
# Author: Henry Nestler <Henry@BigFoot.de>
#
# Simple text based browser for Monotone repositories, written as shell script.
# Can select branch, revision. Views diff from revision, logs, certs and more.
# Base is dialog function and "automate" command of Monotone, with some
# sorting and grepping functionaly.
#
# To use:
#  - Copy this script into a bin PATH
#  - Run from working copy of existing project.
#    Or give full filename to database.
#  - Change your configuration
#    Delete the "VISUAL", to use the "PAGER", deleto both for internal viewer.
#    Save configuration.
#  - Begin with menu "S Select revision"
#  - Browse in branches, revisions, diff files, view logs ...
#
# Needed tools:
#  monotone 0.19 or compatible
#  dialog (tested Version 0.9b)
#  bash, sh, ash, dash
#  less, vi or vim (use $VISUAL or $PAGER)
#  cat, cut, echo, eval, head, sort, tail, wc, xargs ...
#
# History:
# 2005/5/5 Version 0.1.1 Henry@BigFoot.de
# 
# 2005/5/9 Version 0.1.2 Henry@BigFoot.de
# Update for MT 0.19
# Diff from parent.
# Toposort or Date/Time sort, config via TOPOSORT
# 
# 2005/5/13 Version 0.1.3 Henry@BigFoot.de
# Diff from 'parent' mistaken HEAD/REVISION usage.
# Limit count of revisions, change by config menu, default 20 (for big proj).
# 
# 2005/5/24 Version 0.1.4 Henry@BigFoot.de
# Don't run "monotone log" with empty head.
# 
# 2005/5/31 Version 0.1.5 Henry@BigFoot.de
# Add selection for head, if unmerged heads found.
# Short revision hash. Keys in selection. (option)
# Popup select, if more as one parent (from merge).
#
# 2005/6/1 Version 0.1.6 Henry@BigFoot.de
# Autotect for log --depth/--last, new Monotone style version 0.19+.
# Use internal dialog file viewer if VISUAL and PAGER empty.
# Config menu with radiolist. View actual setting.
# Save config only from config menu. No autosave.
# Fix cache deleting on startup.
# Xargs for revision selection with date and key.
#
# 2005/6/6 Version 0.1.7 Henry@BigFoot.de
# Backtitle with head, branch and filename.
# Default-item to remember the selection in menues.
# Check filname for reading before MT fails.
# Exit after --help or --version.
#
# 2005/6/16 Version 0.1.8 Henry@BigFoot.de
# automate ancestors, toposort, complete revision and
# automate parents missing param --db (if no MT directory).
# Switch --depth/--last without error dialog.
# Bug: Select rev for multiple parents (from merge), remove "cat cat".
# Use Author, not Key. Add Branch and Changelog in selection.
# Selectable format for Date, Branch, Author, ChangeLog.  Coloring author.
# Current marker is asterix before date/time.
#
# 2005/6/23 Version 0.1.9 Henry@BigFoot.de
# Cancel is "EXIT" in main menu.
# Meter alongwith --gauge for reading certs :-)
# Config with menu and def.item, instand radiolist and the test on/off thingy.
# Don't remove some old files at exit.
# Branch and head not in Main background title.
#
# 2005/6/24 Version 0.1.10 Henry@BigFoot.de
# Remove TAB's from ChangeLog.
# Some cat as stdin pipe.
# Typofix topsort/toposort.
#
# 2005-06-26 Version 0.1.11 Henry@BigFoot.de
# No double Date/Branch/Author with CR in selection list.
# Short Author and no branche as default in list.
# Set default revision for first selection to head.
# "automate ancestors" without cut (have only one field).
# Internal function for "automate ancestors" with depth limit. Speedup.
#    real    user      sys
# 19.925s  6.780s  13.030s  automate ancestors (net.venge.monotone)
#  6.067s  2.920s   3.120s  automate parents as loop, depth 30
#  4.226s  2.280s   1.910s  automate parents as loop, depth 20
#  1.384s  0.680s   0.700s  automate parents as loop, depth 10

# Known Bugs / ToDo-List:
# * For Monotone Version >0.19  s/--depth/--last/, remove the fallback
# * better make "sed -n -e '1p'" for merge two different branches.

VERSION="0.1.11"

# Save users settings
# Default values, can overwrite on .mtbrowserc
CONFIGFILE="$HOME/.mtbrowserc"

# Store lists for menu here
TEMPDIR="$HOME/.mtbrowse"
TEMPFILE="$TEMPDIR/.tmp"

# Called with filename.
VISUAL="vim -R"

# Called with stdin redirection.
# Set VISUAL empty to use PAGER!
# Don't VISUAL and PAGER to use internal viewer.
PAGER="less"

# 1=Certs Cached, 0=Clean at end (slow and save mode)
CACHE="1"

# T=Toposort revisions, D=Date sort (reverse toposort)
TOPOSORT="T"

# count of certs to get from DB, "0" for all
CERTS_MAX="20"

# Trim hash code
HASH_TRIM="10"

# Format for Date/Time
FORMAT_DATE="L"

# Format Branch Full,Short,None
FORMAT_BRANCH="N"

# Format author (strip domain from mail address)
FORMAT_AUTHOR="S"

# Changelog format
FORMAT_LOG="F"

# Author coloring?
FORMAT_COLOR="\\Z7\\Zb"

# "log --depth=n" was changed to "log --last=n", see
# Author: joelwreed@comcast.net,  Date: 2005-05-30T00:15:27
# Autodetect this and fallback for a while.
# TODO: Remove in future
DEPTH_LAST="--last"

# automate ancestors (I)nteral function, (M)onotone
ANCESTORS="I"

# read saved settings
if [ -f $CONFIGFILE ]
then
    . $CONFIGFILE
fi

# exist working copy?
if [ -f MT/options ]
then
    # Read parameters from file
    #  branch "mtbrowse"
    #database "/home/hn/mtbrowse.db"
    #     key ""

    eval `sed -n -r \
      -e 's/^[ ]*(branch) \"([^\"]+)\"$/\1=\2/p' \
      -e 's/^[ ]*(database) \"([^\"]+)\"$/\1=\2/p' < MT/options`

    if [ -n "$database" ]
    then
	DB=$database
	BRANCH=$branch
    fi
fi


# Simple program args supported
if [ -n "$1" ]
then
    case $1 in
      --version)
	echo "mtbrowse $VERSION"
	exit 0
      ;;
      --help|-h)
	echo "mtbrowse [dbfile]"
	exit 0
      ;;
      *)
	# Databasefile from command line
	DB="$1"
	unset BRANCH

	# MT change the options, if you continue with other DB here!
	if [ -f MT/options ]
	then

	    if ! dialog --cr-wrap \
		--title " *********** WARNING! ********** " \
		--defaultno --colors --yesno "
Your \Zb\Z1MT/options\Zn will be overwrite, if
continue with different DB file or branch
in exist working directory!

YES confirm  /  NO abbort" 0 0
	    then
		echo "abbort"
		exit 1
	    fi
	fi
      ;;
    esac
fi


# Clear cached files
do_clear_cache()
{
    rm -f $TEMPFILE.certs.$BRANCH \
      $TEMPFILE.changelog.$BRANCH
}


# clear temp files
do_clear_on_exit()
{
    rm -f $TEMPFILE.branches $TEMPFILE.ancestors $TEMPFILE.toposort \
      $TEMPFILE.action-select $TEMPFILE.menu $TEMPFILE.input

    if [ "$CACHE" != "1" ]
    then
	do_clear_cache
    fi
}


# View any file (with vim, less or dialog)
do_pager()
{
    if [ -n "$VISUAL" ]
    then
	$VISUAL $1
    elif [ -n "$PAGER" ]
    then
	$PAGER < $1
    else
	dialog --textbox $1 0 0
    fi
    rm $1
}


# Add the date, author and changlog to the list of revisions

# Scanning for:
# Key   : henry@bigfoot.de
# Sig   : ok
# Name  : date
# Value : 2005-05-31T22:29:50		<<---
# --------------------------------------
# Key   : henry@bigfoot.de
# Sig   : ok
# Name  : changelog
# Value : Handle merged parents		<<---

# Output
# 123456 "2005-05-31 22:29 henry@bigfoot.de  Handle merged parents"

fill_date_key()
{
    local in_file=$1
    local out_file=$2
    local short_hash dat bra aut log lineno

    line_count=`wc -l < $in_file`
    if [ "$line_count" -eq 0 ]
    then
	unset line_count
    fi

    lineno=0
    rm -f $out_file
    # Read Key and Date value from certs
    cat $in_file | \
    while read hash
    do
	if [ -n "$line_count" ]
	then
	    let lineno++
	    echo "$(( 100*$lineno/line_count ))"
	else
	    echo -n "." 1>&2
	fi

	short_hash=`echo $hash | cut -c 1-$HASH_TRIM`

	# get all certs of revision
	monotone --db=$DB list certs $hash > $TEMPFILE.c.tmp
		
	# Date format
	case $FORMAT_DATE in
	    F) # 2005-12-31T23:59:59
		dat=`sed -n -r -e \
		    '/^Name  : date/,+1s/Value : (.+)$/ \1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	    L) # 2005-12-31 23:59
		dat=`sed -n -r -e \
		    '/^Name  : date/,+1s/Value : (.{10})T(.{5}).+$/ \1 \2/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	    D) # 2005-12-31
		dat=`sed -n -r -e \
		    '/^Name  : date/,+1s/Value : (.+)T.+$/ \1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	    S) # 12-31 23:59
		dat=`sed -n -r -e \
		    '/^Name  : date/,+1s/Value : .{4}-(.+)T(.{5}).+$/ \1 \2/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	    T) # 23:59:59
		dat=`sed -n -r -e \
		    '/^Name  : date/,+1s/Value : .{10}T(.+{8})$/ \1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	esac

	# Branch format
	case $FORMAT_BRANCH in
	    F) # full
		bra=`sed -n -r -e \
		    '/^Name  : branch/,+1s/Value :(.+)$/\1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	    S) # short
		bra=`sed -n -r -e \
		    '/^Name  : branch/,+1s/Value :.*\.([^\.]+)$/ \1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	esac

	# Author format
	case $FORMAT_AUTHOR in
	    F) # full
		aut=`sed -n -r -e \
		    '/^Name  : author/,+1s/Value : (.+)$/\1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	    S) # short
		aut=`sed -n -r -e \
		    '/^Name  : author/,+1s/Value : (.{1,10}).*@.+$/\1/p' \
		    < $TEMPFILE.c.tmp | sed -n -e '1p'`
		;;
	esac

	# Changelog format
	case $FORMAT_LOG in
	    F) # full     TAB here ----v
		log=`sed -n -r -e "y/\"	/' /" -e \
		    '/^Name  : changelog/,+1s/Value : (.+)$/ \1/p' \
		    < $TEMPFILE.c.tmp`
		;;
	    S) # short
		log=`sed -n -r -e "y/\"	/' /" -e \
		    '/^Name  : changelog/,+1s/Value : (.{1,20}).*$/ \1/p' \
		    < $TEMPFILE.c.tmp`
		;;
	esac

	# Coloring?
	if [ -n "$FORMAT_COLOR" -a "$FORMAT_AUTHOR" != "N" ]
	then
	    # Bug in dialog: Don't allow empty string after \\Zn
	    test -z "$log" && log=" "
	    echo "$short_hash \"$dat$bra $FORMAT_COLOR$aut\\Zn$log\"" \
		>> $out_file
	else
	    echo "$short_hash \"$dat$bra $aut$log\"" >> $out_file
	fi
    done | dialog --gauge "$line_count certs reading" 6 60
    rm $TEMPFILE.c.tmp
}


# Select a branch
# Is parameter given: No user select, if branch known.
do_branch_sel()
{
    local OLD_BRANCH

    if [ ! -f "$DB" ]
    then
	echo "$DB: File not found! (mtbrowse)"
	exit 1
    fi

    if [ ! -r "$DB" ]
    then
	echo "$DB: Can't read file! (mtbrowse)"
	exit 1
    fi

    SHORT_DB=`basename $DB`

    # is Branch set, than can return
    if [ -n "$BRANCH" -a -n "$1" ]
    then
	return
    fi

    # New DB?
    if [ "$DB" != "`cat $TEMPFILE.fname`" ]
    then
	echo "$DB" > $TEMPFILE.fname
	unset BRANCH
    fi
    
    OLD_BRANCH=$BRANCH

    # Get branches from DB
    if [ ! -f $TEMPFILE.branches -o $DB -nt $TEMPFILE.branches \
	-o "$CACHE" != "1" ]
    then
	monotone --db=$DB list branches \
	 | sed -n -r -e 's/^(.+)$/\1\t-/p' > $TEMPFILE.branches \
	 || exit 200
    fi
    
    if [ ! -s $TEMPFILE.branches ]
    then
	echo "Error: No branches found."
	exit 1
    fi

    dialog --begin 1 2 \
	--default-item "$OLD_BRANCH" \
	--menu "Select branch" 0 0 0 \
	`cat $TEMPFILE.branches` \
	2> $TEMPFILE.input
    BRANCH=`cat $TEMPFILE.input`

    # Clear Head, if branch changed
    if [ "$OLD_BRANCH" != "$BRANCH" ]
    then
	# Clear cached files
	do_clear_cache
	do_clear_on_exit
	unset HEAD
	unset SHORT_HEAD
    fi
}


# Get head from DB (need for full log)
# Is parameter given: No user select, if head known.
do_head_sel()
{
    # is Head set, than can return
    if [ -n "$HEAD" -a -n "$1" ]
    then
	return
    fi

    monotone --db=$DB automate heads $BRANCH > $TEMPFILE.heads 2>/dev/null
    # Only one head ?
    if [ `wc -l < $TEMPFILE.heads` -eq 1 -a -n "$1" ]
    then
	HEAD=`head -n 1 < $TEMPFILE.heads`
    else
	# List heads with author and date. Select by user.
	monotone --db=$DB heads --branch=$BRANCH \
	  | sed -n -r -e 's/^([^ ]+) ([^ ]+) ([^ ]+)$/\1 \"\2 \3\"/p' \
	  | xargs dialog --begin 1 2 --menu "Select head" 0 0 0 \
	  2> $TEMPFILE.input
	HEAD=`cat $TEMPFILE.input`
    fi

    # trim for some outputs
    SHORT_HEAD=`echo $HEAD | cut -c 1-$HASH_TRIM`

    rm -f $TEMPFILE.heads
#    do_clear_cache
}


# User menu for current branch
do_action_sel()
{
    # Action-Menu
    while dialog \
	--backtitle "h:$HEAD b:$BRANCH f:$SHORT_DB" \
	--menu "Action for $REVISION" 0 60 0 \
	"L" "Log view of current revision" \
	"P" "Diff files from parent" \
	"W" "Diff files from working copy head" \
	"S" "Diff files from selected revision" \
	"C" "List Certs" \
	"F" "List changed file revision" \
	"-" "-" \
	"Q" "Return" \
	2> $TEMPFILE.action-select
    do

	case `cat $TEMPFILE.action-select` in
	  L)
	    # LOG
	    # 0.19   monotone log --depth=n id file
	    # 0.19+  monotone log --last=n id file
	    if ! monotone --db=$DB log $DEPTH_LAST=1 --revision=$REVISION \
	      > $TEMPFILE.change.log
	    then
		DEPTH_LAST="--depth"
		echo "Fallback to --depth usage."

		# Try again
		monotone --db=$DB log $DEPTH_LAST=1 --revision=$REVISION \
		  > $TEMPFILE.change.log || exit 200
	    fi

	    do_pager $TEMPFILE.change.log
	    ;;
	  P)
	    # DIFF parent
	    monotone --db=$DB automate parents $REVISION > $TEMPFILE.parents

	    if [ `wc -l < $TEMPFILE.parents` -ne 1 ]
	    then
		# multiple parents (from merge)

		# Set DATE/KEY information
		fill_date_key $TEMPFILE.parents $TEMPFILE.certs3tmp

		cat $TEMPFILE.certs3tmp | \
		    xargs dialog --begin 1 2 --colors \
			--default-item "$PARENT" \
			--menu "Select parent for $REVISION" 0 0 0 \
			2> $TEMPFILE.input \
			&& PARENT=`cat $TEMPFILE.input`
		rm $TEMPFILE.certs3tmp
	    else
		# Single parent only
		PARENT=`cat $TEMPFILE.parents`
	    fi
	    rm $TEMPFILE.parents

	    if [ -z "$PARENT" ]
	    then
		dialog --msgbox "No parent found\n$REVISION" 6 45
	    else
		monotone --db=$DB diff \
		  --revision=$PARENT --revision=$REVISION \
		  > $TEMPFILE.parent.diff || exit 200
		do_pager $TEMPFILE.parent.diff
	    fi
	    ;;
	  W)
	    # DIFF
	    # monotone diff --revision=id
	    if [ "$HEAD" = "$REVISION" ]
	    then
		dialog --msgbox "Can't diff with head self\n$HEAD" 6 45
	    else
		# exist working copy?
		if [ -f MT/options ]
		then
		    monotone --db=$DB diff \
		      --revision=$REVISION \
		      > $TEMPFILE.cwd.diff || exit 200
		else
		    # w/o MT dir don't work:
		    # Help MT with HEAD info ;-)
		    monotone --db=$DB diff \
		      --revision=$HEAD --revision=$REVISION \
		      > $TEMPFILE.cwd.diff || exit 200
		fi
		do_pager $TEMPFILE.cwd.diff
	    fi
	    ;;
	  S)
	    # DIFF2: from other revision (not working dir)
	    # Select second revision
	    if cat $TEMPFILE.certs.$BRANCH | \
	      xargs dialog --default-item "$REV2" --colors --menu \
	      "Select _older_ revision for branch:$BRANCH\nrev:$REVISION" \
	      0 0 0  2> $TEMPFILE.revision-select
	    then
		REV2=`cat $TEMPFILE.revision-select`

		# monotone diff --revision=id1 --revision=id2
		monotone --db=$DB diff \
		  --revision=$REV2 --revision=$REVISION \
		  > $TEMPFILE.ref.diff || exit 200
		do_pager $TEMPFILE.ref.diff
	    fi
	    rm -f $TEMPFILE.revision-select
	    ;;
	  C)
	    # List certs
	    monotone --db=$DB list certs $REVISION > $TEMPFILE.certs.log \
	      || exit 200
	    do_pager $TEMPFILE.certs.log
	    ;;
	  F)
	    # List changed files
	    monotone --db=$DB cat revision $REVISION > $TEMPFILE.rev.changed \
	      || exit 200
	    do_pager $TEMPFILE.rev.changed
	    ;;
	  Q)
	    # Menu return
	    return
	    ;;
	esac
    done
}

# Get parents recursive.
# Same as automate ancestors, but limit the depth
# Function called recursive!
do_automate_ancestors_depth()
{
	locale depth head rev

	depth=$1
	head=$2

	# Empty parm?
	if [ -z "$depth" -o -z "$depth" ]
	then
		return 0
	fi

	# Limit by depth?
	if [ "$depth" -gt $CERTS_MAX -o "$depth" -gt 200 ]
	then
		return 0
	fi

	let depth++
	monotone --db=$DB automate parents $head |\
	while read rev
	do
	    if ! grep -q -l -e "$rev" $TEMPFILE.ancestors
	    then
		echo "$rev" >> $TEMPFILE.ancestors
		do_automate_ancestors_depth $depth $rev || return $?
	    fi
	done
	let depth--

	return 0
}

# Select a revision
do_revision_sel()
{
    local SHORT_REV

    # if branch or head not known, ask user
    echo "branch check..."
    do_branch_sel check
    echo "head check..."
    do_head_sel check

    # Building revisions list
    if [ ! -f $TEMPFILE.certs.$BRANCH -o $DB -nt $TEMPFILE.certs.$BRANCH ]
    then
	echo "Reading ancestors ($HEAD)"
	echo "$HEAD" > $TEMPFILE.ancestors

	if [ "$ANCESTORS" = "I" -a "$CERTS_MAX" -gt 0 ]
	then
	    do_automate_ancestors_depth 1 $HEAD || exit 200
	else
	    monotone --db=$DB automate ancestors $HEAD \
	      >> $TEMPFILE.ancestors || exit 200
	fi

	if [ "$TOPOSORT" = "T" -o "$CERTS_MAX" -gt 0 ]
	then
		echo "Toposort..."
		monotone --db=$DB automate toposort `cat $TEMPFILE.ancestors` \
		  > $TEMPFILE.toposort || exit 200

		if [ "$CERTS_MAX" -gt 0 ]
		then
			# Only last certs. Remember: Last line is newest!
			tail -n "$CERTS_MAX" < $TEMPFILE.toposort \
			  > $TEMPFILE.toposort2
			mv $TEMPFILE.toposort2 $TEMPFILE.toposort
		fi
	else
		mv $TEMPFILE.ancestors $TEMPFILE.toposort
	fi

	# Reading revisions and fill with date
	fill_date_key $TEMPFILE.toposort $TEMPFILE.certs3tmp

	if [ "$TOPOSORT" != "T" ]
	then
		# Sort by date+time
		sort -k 2 -r < $TEMPFILE.certs3tmp > $TEMPFILE.certs.$BRANCH
		rm $TEMPFILE.certs3tmp
	else
		mv $TEMPFILE.certs3tmp $TEMPFILE.certs.$BRANCH
	fi
    fi

    # if first rev is empty, use head instand
    if [ -z "$REVISION" ]
    then
	SHORT_REV=`echo $HEAD | cut -c 1-$HASH_TRIM`
    else
	SHORT_REV=`echo $REVISION | cut -c 1-$HASH_TRIM`
    fi

    # Select revision
    while cat $TEMPFILE.certs.$BRANCH | \
	xargs dialog \
	 --backtitle "h:$HEAD b:$BRANCH f:$SHORT_DB" \
	 --no-shadow \
	 --colors \
	 --default-item "$SHORT_REV" \
	 --menu "Select revision for branch:$BRANCH" \
	 0 0 0  2> $TEMPFILE.revision-select
    do
	SHORT_REV=`cat $TEMPFILE.revision-select`

	# Remove old marker, set new marker
	sed -r \
	  -e "s/^(.+\")\*(.+)\$/\1 \2/" \
	  -e "s/^($SHORT_REV.* \") (.+)\$/\1\*\2/" \
	  < $TEMPFILE.certs.$BRANCH > $TEMPFILE.certs.$BRANCH.base
	mv $TEMPFILE.certs.$BRANCH.base $TEMPFILE.certs.$BRANCH

	# Error, on "monotone automate parent XXXXXX", if short revision.  :-(
	# Expand revision here, if short revision (is alway short now)
	REVISION=`monotone --db=$DB complete revision $SHORT_REV`

	# OK Button: Sub Menu
	do_action_sel
    done
    rm -f $TEMPFILE.revision-select
}


# Menu for configuration
do_config()
{
    local item

    while dialog --default-item "$item" \
	--menu "Configuration" 0 0 0 \
	"V" "VISUAL [$VISUAL]" \
	"Vd" "Set VISUAL default to vim -R" \
	"P" "PAGER  [$PAGER]" \
	"Pd" "set PAGER default to less" \
	"S" "Sort by Toposort or Date [$TOPOSORT]" \
	"T" "Time and date format [$FORMAT_DATE]" \
	"B" "Branch format [$FORMAT_BRANCH]" \
	"A" "Author format [$FORMAT_AUTHOR]" \
	"Ac" "Author Color format [$FORMAT_COLOR]" \
	"L" "changeLog format [$FORMAT_LOG]" \
	"D" "Depth limit for ancestors [$ANCESTORS]" \
	"C" "Certs limit in Select-List [$CERTS_MAX]" \
	"-" "-" \
	"W" "Write configuration file" \
	"R" "Return to main menu" \
	2> $TEMPFILE.menu
    do
	item=`cat $TEMPFILE.menu`
	case $item in
	  V)
	    # Setup for VISUAL
	    if dialog --inputbox \
		"Config for file viewer\nuse in sample: \"vim -R changes.diff\"" \
		8 70 "$VISUAL" 2> $TEMPFILE.input
	    then
		VISUAL=`cat $TEMPFILE.input`
	    fi
	    ;;
	  Vd)
	    # set Visual default
	    VISUAL="vim -R"
	    ;;
	  P)
	    # Setup for PAGER
	    if dialog --inputbox \
		"Config for pipe pager\nuse in sample: \"monotone log | less\"" \
		8 70 "$PAGER" 2> $TEMPFILE.input
	    then
		PAGER=`cat $TEMPFILE.input`
	    fi
	    ;;
	  Pd)
	    # set Pager default
	    PAGER="less"
	    ;;
	  S)
	    # change T=Toposort revisions, D=Date sort (reverse toposort)
	    if dialog --default-item "$TOPOSORT" \
		--menu "Sort revisions by" 0 0 0 \
		"T" "Toposort, oldest top (from Monotone)" \
		"D" "Date/Time (reverse toposort)" \
		2> $TEMPFILE.input
	    then
		TOPOSORT=`cat $TEMPFILE.input`
	    fi
	    ;;
	  T)
	    # change date/time format
	    if dialog --default-item "$FORMAT_DATE" \
		--menu "Format for date and time" 0 0 0 \
		"F" "2005-12-31T23:59:59 -- Full date and time" \
		"L" "2005-12-31 23:59    -- Long date and time" \
		"D" "2005-21-31          -- Date only" \
		"S" "12-31 23:59:59      -- Short date and time" \
		"T" "23:59:59            -- Time only" \
		"N" "no date and no time" \
		2> $TEMPFILE.input
	    then
		FORMAT_DATE=`cat $TEMPFILE.input`
	    fi
	    ;;
	  B)
	    # change branch format
	    if dialog --default-item "$FORMAT_BRANCH" \
		--menu "Format for branch" 0 0 0 \
		"F" "Full branch" \
		"S" "Short branch, right side only" \
		"N" "no branch" \
		2> $TEMPFILE.input
	    then
		FORMAT_BRANCH=`cat $TEMPFILE.input`
	    fi
	    ;;
	  A)
	    # change author's format
	    if dialog --default-item "$FORMAT_AUTHOR" \
		--menu "Format for author" 0 0 0 \
		"F" "Full author" \
		"S" "Short author, strip domain from email address" \
		"N" "no author" \
		2> $TEMPFILE.input
	    then
		FORMAT_AUTHOR=`cat $TEMPFILE.input`
	    fi
	    ;;
	  Ac)
	    # Author coloring
	    if dialog --default-item \
		"`test -n \"$FORMAT_COLOR\" && echo \"yes\" || echo \"no\"`" \
		--menu "Color author in selecetion" 0 0 0 \
		"yes" "author is color" \
		"no" "author has no special color" \
		2> $TEMPFILE.input
	    then
		if [ "`cat $TEMPFILE.input`" = "yes" ]
		then
		    dialog --colors \
		     --default-item "$FORMAT_COLOR" \
		     --menu "Selecet color for author" 0 0 0 \
			"\\Z0" "\Z0Color\Zn 0" \
			"\\Z1" "\Z1Color\Zn 1" \
			"\\Z2" "\Z2Color\Zn 2" \
			"\\Z3" "\Z3Color\Zn 3" \
			"\\Z4" "\Z4Color\Zn 4" \
			"\\Z5" "\Z5Color\Zn 5" \
			"\\Z6" "\Z6Color\Zn 6" \
			"\\Z7" "\Z7Color\Zn 7" \
			"\\Zb\\Z0" "\Zb\Z0Color\Zn 0b" \
			"\\Zb\\Z1" "\Zb\Z1Color\Zn 1b" \
			"\\Zb\\Z2" "\Zb\Z2Color\Zn 2b" \
			"\\Zb\\Z3" "\Zb\Z3Color\Zn 3b" \
			"\\Zb\\Z4" "\Zb\Z4Color\Zn 4b" \
			"\\Zb\\Z5" "\Zb\Z5Color\Zn 5b" \
			"\\Zb\\Z6" "\Zb\Z6Color\Zn 6b" \
			"\\Zb\\Z7" "\Zb\Z7Color\Zn 7b" \
			"\\Zb" "\ZbBold\Zn b" \
			"\\Zu" "\ZuUnderline\Zn u" \
			2> $TEMPFILE.input \
		    && FORMAT_COLOR=`cat $TEMPFILE.input`
		else
		    FORMAT_COLOR=""
		fi
	    fi
	    ;;
	  L)
	    # Changelog format
	    dialog \
		--default-item "$FORMAT_LOG" \
		--menu "Format for ChangeLog in selection" 0 0 0 \
		"F" "Full changelog line" \
		"S" "Short changelog" \
		"N" "no changelog in selection" \
		2> $TEMPFILE.input \
		&& FORMAT_LOG=`cat $TEMPFILE.input`
	    ;;
	  D)
	    # automate ancestors (I)nteral function, (M)onotone
	    if dialog --default-item "$ANCESTORS" \
		--menu "Get ancestors by using" 0 0 0 \
		"M" "Monotone \"automate ancestor\" (save mode)" \
		"I" "Internal function with depth limit (faster)" \
		2> $TEMPFILE.input
	    then
		ANCESTORS=`cat $TEMPFILE.input`
	    fi
	    ;;
	  C)
	    # Change CERTS_MAX
	    dialog --inputbox \
	      "Set maximum lines for revision selction menu\n(0=disabled)" \
	      9 70 "$CERTS_MAX" 2> $TEMPFILE.input \
	      && CERTS_MAX=`cat $TEMPFILE.input`
	    ;;
	  W)
	    # Save environment
	    cat > $CONFIGFILE << EOF
# File: ~/.mtbrowserc

DB="$DB"
BRANCH="$BRANCH"
VISUAL="$VISUAL"
PAGER="$PAGER"
TEMPDIR="$TEMPDIR"
TEMPFILE="$TEMPFILE"
TOPOSORT="$TOPOSORT"
CACHE="$CACHE"
CERTS_MAX="$CERTS_MAX"
DEPTH_LAST="$DEPTH_LAST"
FORMAT_DATE="$FORMAT_DATE"
FORMAT_BRANCH="$FORMAT_BRANCH"
FORMAT_AUTHOR="$FORMAT_AUTHOR"
FORMAT_LOG="$FORMAT_LOG"
FORMAT_COLOR="$FORMAT_COLOR"
ANCESTORS="$ANCESTORS"
EOF
		dialog --title " Info " --sleep 2 --infobox \
		    "Configration wrote to\n$CONFIGFILE" 0 0
	    echo "config saved"
    	    ;;
	  *)
	    # Return to Main
	    rm -f $TEMPFILE.input
	    return
	    ;;
	esac
    done
}

# Is dialog installed?
if ! dialog --version </dev/null >/dev/null 2>&1
then
    # Hm, need this here
    echo
    echo "dialog - display dialog boxes from shell scripts."
    echo "Dialog is needed for this tool, please install it!"
    echo
    exit -1
fi

mkdir -p $TEMPDIR

while dialog \
	--cancel-label "Exit" \
	--backtitle "$DB" \
	--menu "Main - mtbrowse v$VERSION" 0 0 0 \
	"S" "Select revision" \
	"I" "Input revision" \
	"F" "Change DB File [`basename $DB`]" \
	"B" "Branch select  [$BRANCH]" \
	"H" "Head select    [$SHORT_HEAD]" \
	"R" "Reload DB, clear cache" \
	"-" "-" \
	"l" "Sumary complete log" \
	"t" "List Tags" \
	"h" "List Heads" \
	"k" "List Keys" \
	"-" "-" \
	"C" "Configuration" \
	"-" "-" \
	"X" "eXit" \
	2> $TEMPFILE.menu
do
    case `cat $TEMPFILE.menu` in
      S)
	# Revision selection
	do_revision_sel
	;;
      I)
	# Input Revision
	if dialog --inputbox \
	  "Input 5 to 40 digits of known revision" 8 60 "$REVISION" \
	  2> $TEMPFILE.input
	then
	    REVISION=`cat $TEMPFILE.input`

	    do_action_sel
	    do_revision_sel
	fi
	;;
      R)
	# Cache del and Revision selection
	do_clear_cache
	do_revision_sel
	;;
      B)
	# Branch config
	rm -f $TEMPFILE.branches
	do_branch_sel
	;;
      H)
        # Select head
	# if branch or head not known, ask user
	do_branch_sel check
	do_head_sel
	do_clear_cache
	;;
      F)
	# Change DB file
	DNAME=`dirname $DB`
	if [ -z "$DNAME" ]
	then
	    DNAME=`pwd`
	fi
	
	if dialog --fselect $DNAME/`basename $DB` 15 70 2> $TEMPFILE.name-db
	then
	    DB=`cat $TEMPFILE.name-db`
	    dialog --msgbox "file changed to\n$DB" 0 0
	    unset BRANCH
	else
	    dialog --msgbox "filename unchanged" 0 0
	fi
	rm -f $TEMPFILE.name-db
	;;
      C)
	do_config
	# Clear cache
	do_clear_cache
	;;
      l)
	# Sumary coplete LOG
	# if not branch known, ask user
	do_branch_sel check
	do_head_sel check

	if [ ! -f $TEMPFILE.changelog.$BRANCH -o \
	    $DB -nt $TEMPFILE.changelog.$BRANCH ]
	then
	    echo "Reading log...($BRANCH)"
	    monotone --db=$DB log --revision=$HEAD \
	      > $TEMPFILE.changelog.$BRANCH || exit 200
	fi
	cp $TEMPFILE.changelog.$BRANCH $TEMPFILE.change.log
	do_pager $TEMPFILE.change.log
	;;
      t)
	# List Tags
	echo "Reading Tags..."
	monotone --db=$DB list tags > $TEMPFILE.tags.log || exit 200
	do_pager $TEMPFILE.tags.log
	;;
      h)
	# if not branch known, ask user
	do_branch_sel check

	monotone --db=$DB heads --branch=$BRANCH > $TEMPFILE.txt || exit 200
	do_pager $TEMPFILE.txt
	;;
      k)
	# List keys
	monotone --db=$DB list keys > $TEMPFILE.txt || exit 200
	do_pager $TEMPFILE.txt
	;;
      X)
	do_clear_on_exit
	clear
	exit 0
        ;;
      *)
	echo "Error in Menu!"
	exit 250
        ;;
    esac
done

do_clear_on_exit
clear
