;;; monotone.el --- Run monotone from within Emacs.
;;
;; Copyright 2005 by Olivier Andrieu <oandrieu@nerim.net>
;; Version 0.2 2005-04-13
;;
;; Licence: GPL v2 (same as monotone)
;; Keymaps and other stuff added by harley@panix.com
;;

;;; Commentary:
;;
;; To use monotone from within Emacs, all you should need to
;; do is require the package.  There are many options but
;; then only one you should have to set is the prefix key for
;; the keybindings.  Something like the following should work:
;;
;;   (require 'monotone)
;;   (monotone-set-vc-prefix-key [f5]) ;; or "\C-xv"
;;   (setq monotone-passwd-remember t)
;;
;; You may want to put "monotone-grab-id" on a handy function key.
;;
;; Monotone prefers to work with the global working set.
;; "monotone.el" has its defaults set to match.
;; Commands run without a prefix work on the global working set.
;; One C-u is the subtree (".") and C-u C-u is the current file.
;; (There are exceptions)
;;
;; Some of the function names follow Emacs "vc-" names,
;; others follow monotone names.  I havent decided which I
;; like better.
;;
;; This mode was written and tested with GNU Emacs 21.3.50.1

;; FIXME: handle aborts better and kill monotone.
;; FIXME: given an id, suck out the file with "monotone cat"
;; FIXME: handle diff --revision XXX path/to/file

;;; User vars:
;; These vars are likley to be changed by the user.

(defvar monotone-program "monotone"
  "*The path to the monotone program.")

(defvar monotone-passwd-remember nil
  "*Should Emacs remember your monotone passwords?
This is a security risk as it could be extracted from memory or core dumps.")

(defvar monotone-passwd-alist nil
  "*The password to be used when monotone asks for one.
List of of (pubkey_id . password ).
If `monotone-passwd-remember' is t it will be remembered here.")

;; This is set to [f5] for testing.
;; Should be nil for general release, as we dont want to
;; remove keys without the users consent.
(defvar monotone-vc-prefix-key  nil ;; [f5] "\C-xv" nil
  "The prefix key to use for the monotone vc key map.
You may wish to change this before loading monotone.el.
Habitual monotone users can set it to '\C-xv'.")

(defvar monotone-menu-name "Monotone"
  "The name of the monotone menu.")


;;; System Vars:
;; It is unlikely for users to need to change these.

(defvar monotone-last-id nil
  "The last id which was worked with or grabbed.
This could be a file, manifest or revision.
It is also stuffed into the kill ring.
This is used for defaults.")
(defvar monotone-last-fileid nil
  "The last file id.")
(defvar monotone-last-manifestid nil
  "The last manifest id.")
(defvar monotone-last-revisionid nil
  "The last revision id.")

(defvar monotone-buffer "*monotone*"
  "The buffer used for running monotone commands.")

(defvar monotone-commit-buffer "*monotone commit*"
  "The name of the buffer for the commit message.")
(defvar monotone-commit-edit-status nil
  "The sentinel for completion of editing the log.")
(make-variable-buffer-local 'monotone-commit-edit-status)
(defvar monotone-commit-args nil
  "The args for the commit.")
(make-variable-buffer-local 'monotone-commit-args)

(defvar monotone-cmd-last-args nil
  "The args for the last command.")
;;(make-variable-buffer-local 'monotone-cmd-args)


(defvar monotone-commit-dir nil)

(defvar monotone-wait-time 5
  "Time to wait for monotone to produce output.")

(defvar monotone-MT-top nil
  "The directory which contains the MT directory.
This is used to pass state -- best be left nil.")

(defvar monotone-log-depth 100
  "The depth to limit output of 'monotone log' entries.
Zero is unlimited.")

;;; monotone-commit-mode is used when editing the commit message.
(defvar monotone-commit-mode nil)
(make-variable-buffer-local 'monotone-commit-mode)

(defvar monotone-commit-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-c" 'monotone-commit-complete)
    map))

;; hook it in
(add-to-list 'minor-mode-alist '(monotone-commit-mode " Monotone Commit"))
(add-to-list 'minor-mode-map-alist (cons 'monotone-commit-mode monotone-commit-mode-map))

(defvar monotone-msg nil
  "When non-nil log stuff to *Messages*.")

(defmacro monotone-msg (&rest args)
  "Print ARGS to *Messages* when variable `monotone-msg' is non-nil."
  `(when monotone-msg
     (message ,@args)))
;; (monotone-msg "%s" '("foo" 1 2 3))

(defvar monotone-output-mode-hook nil
  "*The hook for monotone output.")

(defvar monotone-commit-instructions
  "--------------------------------------------------
Enter Log.  Lines beginning with 'MT:' are removed automatically.
Type C-c C-c to commit, kill the buffer to abort.
--------------------------------------------------"
  "Instructional text to insert into the commit buffer.
'MT: ' is added when inserted.")

(defvar monotone-commit-mode-hook nil
  "*The hook for function `monotone-commit-mode'.")


(defvar monotone-server nil
  "The default server for pulls and pushes.")
(defvar monotone-collection nil
  "The default collection for pulls and pushes.")
(defvar monotone-server-hist nil
  "A history of servers.")
(defvar monotone-collection-hist nil
  "A history of collections.")

;;; Key maps
(defvar monotone-vc-prefix-map
  (let ((map (make-sparse-keymap)))
    (define-key map "="    'monotone-vc-diff)
    (define-key map "P"    'monotone-push)
    (define-key map "\C-q" 'monotone-vc-commit)
    (define-key map "i"    'monotone-vc-register)
    (define-key map "6"    'monotone-grab-id)
    (define-key map "l"    'monotone-vc-print-log)
    (define-key map "p"    'monotone-pull)
    (define-key map "q"    'monotone-vc-commit) ;; i am a lazy typist
    (define-key map "s"    'monotone-vc-status)
    map))
(fset 'monotone-vc-prefix-map monotone-vc-prefix-map)

;;; Code:

(defun monotone-set-vc-prefix-key (key)
  "Set KEY to be the prefix for monotone in the global keymap."
  (setq monotone-vc-prefix-key key)
  (define-key global-map monotone-vc-prefix-key 'monotone-vc-prefix-map))

;; install it if set.
(when monotone-vc-prefix-key
  (monotone-set-vc-prefix-key monotone-vc-prefix-key))


(defun monotone-toggle-vc-prefix-map (&optional arg)
  "Toggle between the default and monotone vc-maps, ARG set map.
With arg 0 use the default variable `vc-prefix-map'.
With t use `monotone-vc-prefix-map'.
This permits quick switches between the classic vc and monotone keymaps."
  (interactive "P")
  (message "Arg: %s" arg)
  (define-key ctl-x-map "v"
    (let ((current (lookup-key ctl-x-map "v")))
      (if (and (not (eq arg 0))
               (or arg (not (eq current monotone-vc-prefix-map))))
        monotone-vc-prefix-map
        vc-prefix-map))))
;; (monotone-toggle-vc-prefix-map t)

;; Utility functions
(defun monotone-file-parent-directory (file)
  "Return the parent directory of FILE."
  (file-name-directory (directory-file-name file)))

(defun monotone-find-MT-top (&optional path)
  "Find the directory which contains the 'MT' directory.
Optional argument PATH ."
  (setq path (or path (buffer-file-name) default-directory))
  (when (null path)
    (error "Cant find top for %s" path))
  ;; work with full path names
  (setq path (expand-file-name path))
  (block nil
    (let ((prev-path nil))
      (while (not (equal path prev-path))
        (let ((mt-dir (concat path "MT")))
          ;;(message "Search: %s" mt-dir)
          (when (file-directory-p mt-dir)
            (return path))
          (setq prev-path path
                path (monotone-file-parent-directory path)))))))
;;(monotone-find-MT-top "/disk/amelie1/harley/monotone-dev/contrib/monotone.el")

(defun monotone-extract-MT-path (path &optional mt-top)
  "Get the PATH minus the MT-TOP."
  ;; cast and check
  (when (bufferp path)
    (setq path (buffer-file-name path)))
  (when (not (stringp path))
    (error "path is not a string."))
  (let ((mt-top (or mt-top monotone-MT-top (monotone-find-MT-top path))))
    ;; work with full names
    (setq path (expand-file-name path)
          mt-top (expand-file-name mt-top))
    (if (not mt-top)
      nil
      (substring path (length mt-top)))))

;; (monotone-extract-MT-path "/disk/amelie1/harley/monotone-dev/contrib/monotone.el")
;; (monotone-find-MT-dir "/disk/amelie1/harley")
;; (monotone-extract-MT-path (current-buffer))

(defun monotone-output-mode ()
  "In the future this will provide some fontification.
Nothing for now."
  (interactive)
  (fundamental-mode) ;;(text-mode)
  (run-hooks monotone-output-mode-hook))

;;(define-derived-mode monotone-shell-mode comint-mode "Monotone")

;; Run a monotone command
(defun monotone-cmd (args)
  "Execute the monotone command with ARGS in the monotone top directory."
  (monotone-msg "%s" args)
  ;; coerce args to what we expect
  (when (stringp args)
    (setq args (split-string args nil)))
  (when (not (listp args))
    (setq args (list args)))
  ;;
  (let ((mt-top (or monotone-MT-top (monotone-find-MT-top)))
        (mt-buf (get-buffer-create monotone-buffer))
        ;;(mt-pgm "ls") ;; easy debugging
        (mt-pgm monotone-program)
        monotone-MT-top
        mt-cmd mt-status)
    ;; where to run
    (when (or (not (stringp mt-top)) (not (file-directory-p mt-top)))
      (setq mt-top (monotone-find-MT-top))
      (when (or (not (stringp mt-top)) (not (file-directory-p mt-top)))
        (error "Unable to find the MT top directory")))
    (setq monotone-MT-top mt-top)
    ;; show the window
    ;;(if (not (equal (current-buffer) mt-buf))
    (switch-to-buffer-other-window mt-buf) ;;)
    (sit-for 0)
    (set-buffer mt-buf)
    ;; still going?
    (when (get-buffer-process mt-buf)
      (error "Monotone is currently running"))
    ;; prep the buffer for output
    (toggle-read-only -1)
    (erase-buffer)
    ;;(buffer-disable-undo (current-buffer))
    (setq default-directory mt-top)
    ;; remeber the args
    (setq monotone-cmd-last-args args)
    ;; run
    (let ((p (apply #'start-process monotone-buffer mt-buf mt-pgm args)))
      (while (eq (process-status p) 'run)
        ;; FIXME: rather than printing messages, abort after too long a wait.
        (when (not (accept-process-output p monotone-wait-time))
          ;;(message "waiting for monotone..."))
          ;; update the screen
          (goto-char (point-max))
          (sit-for 0)
          ;; look for passwd prompt
          (beginning-of-line)
          (when (looking-at "^enter passphrase for key ID \\[\\(.*\\)\\]")
            (let ((pass (monotone-passwd-prompt (match-string 1))))
              ;;(end-of-line)
              ;;(insert "********\n") ;; filler text
              (process-send-string p pass)
              (process-send-string p "\n"))))
        (setq mt-status (process-exit-status p)))
      ;; make the buffer nice.
      (goto-char (point-min))
      (view-mode)
      ;; FIXME: (set-buffer-modified-p nil)
      ;; did we part on good terms?
      (when (not (zerop mt-status))
        (message "%s: exited with status %s" mt-pgm mt-status)
        (beep)
        (sit-for 3))
      mt-status)))

;; (monotone-cmd '("list" "branches"))
;; (monotone-cmd '("list" "keys"))
;; (monotone-cmd "pubkey harley@panix.com")
;; (monotone-cmd '("status error"))

(defun monotone-cmd-hide (args)
  "Run monotone with ARGS without showing the output."
  (save-window-excursion
    (monotone-cmd args)))

;; run
(defun monotone (string)
  "Prompt for a STRING and run monotone with the split string."
  (interactive "smonotone ")
  (monotone-cmd string))

(defun monotone-rerun ()
  "Rerun the last monotone command."
  (interactive)
  (let ((args monotone-cmd-last-args))
    (when (or (null args) (not (listp args)))
      (error "No last args to rerun"))
    (monotone-cmd args)))
;; (monotone-cmd "list known")

(defun monotone-cmd-is-running ()
  "Return  if monotone is running."
  (save-window-excursion
    (let ((buf (get-buffer-create monotone-buffer)))
      (get-buffer-process buf))))
;; (monotone-cmd-is-running)


;;;;;;;;;;
(defun monotone-passwd-remember (keypairid password)
  "Remember the PASSWORD for KEYPAIRID."
  (let ((rec (assoc keypairid monotone-passwd-alist)))
    (if rec
      (setcdr rec password)
      (progn
        (setq rec (cons keypairid password))
        (setq monotone-passwd-alist (cons rec monotone-passwd-alist))))
    rec))
;; (monotone-passwd-remember "foo" "bar")
;; (setq monotone-passwd-alist nil)

(defun monotone-passwd-find (keypairid)
  "Return the password for KEYPAIRID or nil."
  (cdr (assoc keypairid monotone-passwd-alist)))
;; (monotone-passwd-find "foo")

(defun monotone-passwd-prompt (keypairid)
  "Read the password for KEYPAIRID."
  (let ((passwd (monotone-passwd-find keypairid))
        prompt)
    (setq prompt (format "Password for '%s'%s: " keypairid
                         (if passwd " [return for default]" "")))
    (setq passwd (read-passwd prompt nil passwd))
    (when monotone-passwd-remember
      (monotone-passwd-remember keypairid passwd))
    passwd))
;; (monotone-passwd-prompt "foo@bar.com")
;; (setq monotone-passwd-remember t)

;;
(defun monotone-list-branches ()
  "List the monotone branches known."
  (interactive)
  (monotone-cmd '("list" "branches")))

;;;;;;;;;;

(defun monotone-db-prompt ()
  "Prompt for the server and collection, defaulting to the prior values."
  ;; read-string docs say not to use initial-input but "compile" does.
  (setq monotone-server
        (read-string "Monotone server [host:port]: " monotone-server
                     'monotone-server-hist))
  (setq monotone-collection
        (read-string "Monotone collection: " monotone-collection
                     'monotone-collection-hist)))

(defun monotone-db-action (prefix action)
  "Preform the db ACTION requested.  With PREFIX prompt for info."
  (when (equal prefix 0)
    (setq monotone-server     nil
          monotone-collection nil))
  (when prefix
    (monotone-db-prompt))
  ;;
  (let ((cmd (list "--ticker=dot" (format "%s" action)))
        (svr (or monotone-server ""))
        (col (or monotone-collection "")))
    ;; given address?
    (when (and (stringp svr) (not (string= svr "")))
      (setq cmd (append cmd (list svr)))
      ;; given collection?
      (when (and (stringp col) (not (string= col "")))
        (setq cmd (append cmd (list col)))))
    ;;
    (monotone-cmd cmd)))

(defun monotone-pull (arg)
  "Pull updates from a remote server.  ARG prompts.
With ARG prompt for server and collection.
With ARG of 0, clear default server and collection."
  (interactive "P")
  (monotone-db-action arg "pull"))

(defun monotone-push (arg)
  "Push the DB contents to a remote server.  ARG prompts."
  (interactive "P")
  (monotone-db-action arg "push"))

(defun monotone-sync (arg)
  "Sync the DB with a remote server.  ARG prompts."
  (interactive "P")
  (monotone-db-action arg "sync"))

;;;;;;;;;;

;;; Start if the commit process...
;; FIXME: the default should be a global commit.
(defun monotone-vc-commit (args)
  "Do a commit."
  (interactive "p")
  (setq args (monotone-arg-decode args))
  (when (eq args 'file)
    (when (not (setq args (buffer-file-name)))
      (error "Cant commit a buffer without a filename")))
  ;; dont run two processes
  (when (monotone-cmd-is-running)
    (switch-to-buffer (get-buffer-create monotone-buffer))
    (error "You have a monotone process running"))
  ;; flush buffers
  (save-some-buffers)
  ;;
  (let ((buf (get-buffer-create monotone-commit-buffer))
        (monotone-MT-top (monotone-find-MT-top)))
    ;; found MT?
    (when (not monotone-MT-top)
      (error "Cant find MT directory"))
    ;; show it
    (when (not (equal (current-buffer) buf))
      (switch-to-buffer-other-window buf))
    (set-buffer buf)
    (toggle-read-only -1)
    ;; Have the contents been commited?
    (when (eq monotone-commit-edit-status 'started)
      (message "Continuing commit message already started."))
    (when (or (null monotone-commit-edit-status) (eq monotone-commit-edit-status 'done))
      (erase-buffer)
      (setq default-directory monotone-MT-top)
      (let ((mt-log-path (concat monotone-MT-top "MT/log")))
        (when (file-readable-p mt-log-path)
          (insert-file mt-log-path)))
      ;; blank line for user to type
      (beginning-of-buffer)
      (insert "\n")
      (beginning-of-buffer)
      (monotone-commit-mode))
    ;; update the "MT:" lines by replacing them.
    (monotone-remove-MT-lines)
    (end-of-buffer)
    (when (not (looking-at "^"))
      (insert "\n"))
    (let ((eo-message (point)))
      ;; what is being commited?
      ;;(mapc (function (lambda (a) (insert "args: " (format "%s" a) "\n"))) args)
      (insert (format "%s\n" args))
      ;;(insert (format "Commit arg = %s" arg) "\n")
      ;; instructional text
      (when (stringp monotone-commit-instructions)
        (insert monotone-commit-instructions)
        (when (not (looking-at "^"))
          (insert "\n")))
      ;; what is being committed?
      ;; FIXME: handle args -- this is doing a global status
      (monotone-cmd-hide "status")
      (insert-buffer-substring monotone-buffer)
      ;; insert "MT: " prefix
      (goto-char eo-message)
      (while (search-forward-regexp "^" (point-max) t)
        (insert "MT: ")))
    ;; ready for edit -- put this last avoid being cleared on mode switch.
    (beginning-of-buffer)
    (setq monotone-commit-edit-status 'started
          monotone-commit-args args)))

(defun monotone-commit-mode (&optional arg)
  "Mode for editing a monotone commit message.  ARG turns on."
  (interactive "p")
  (fundamental-mode) ;; (text-mode)
  (run-hooks monotone-commit-mode-hook)
  ;; must be last to avoid being cleared.
  (setq monotone-commit-mode t))
;; (if (null arg)
;; (not monotone-commit-mode)
;; (> (prefix-numeric-value arg) 0)))
;; (when monotone-commit-mode
;; turn on the minor mode for keybindings and run hooks.


(defun monotone-commit-complete ()
  "Complete the message and commit the work."
  (interactive)
  (when (not (eq monotone-commit-edit-status 'started))
    (error "The commit in this buffer is '%s'" monotone-commit-edit-status))
  (monotone-remove-MT-lines)
  (let ((buf (current-buffer))
        (message (buffer-substring-no-properties (point-min) (point-max)))
        (mca     monotone-commit-args) ;; copy of buffer-local-var
        (args (list "commit")))
    (switch-to-buffer (get-buffer-create monotone-buffer))
    ;; assemble and run the command
    (setq args (append args (list  "--message" message)))
    ;; FIXME: global subtree file list...
    (cond
     ((equal mca 'global)
      (monotone-cmd args)) ;; no spec
     ((equal mca 'tree)
      (error "Monotone tree scope sucks for commit!"))
     ((stringp mca) ;; file
      (setq args (append args (list (monotone-extract-MT-path mca))))
      (monotone-cmd args))
     (t
      (error "unknown monotone-commit-args")))
    ;; mark it done
    (set-buffer buf)
    (setq monotone-commit-edit-status 'done)))

(defun monotone-remove-MT-lines ()
  "Remove lines starting with 'MT:' from the buffer."
  ;; doesnt need to be (interactive)
  (beginning-of-buffer)
  (while (search-forward-regexp "^MT:.*$" (point-max) t)
    (beginning-of-line)
    (kill-line 1)))

;;;;;;;;;;

(defun monotone-arg-decode (arg)
  "Decode the ARG into the scope monotone should work on."
  (interactive "p")
  (message "%s" arg)
  (cond
   ((member arg '(global tree file)) arg) ;; identity
   ((= arg  1) 'global)
   ((= arg  4) 'tree)
   ((= arg 16) 'file)
   (t (error "Prefix should be in (1,4,16) or (global tree file)"))))
;; (monotone-arg-decode 4)
;; (monotone-arg-decode 'file)

;;(defun monotone-arg-scope (scope filename)
;;  "Turn the SCOPE and FILENAME into and arg for monotone."
;;  (when (numberp scope)
;;    (setq scope (monotone-arg-decode scope)))
;;  (when (bufferp filename)
;;    (setq filename (buffer-file-name filename)))
;;  (cond
;;   ((eq scope 'global) nil)
;;   ((eq scope 'tree) ".")
;;   ((eq scope 'file)
;;    (if filename
;;      (monotone-extract-MT-path filename
;;   (t (error "Bad scope: %s" scope))))
;;;; (monotone-arg-scope 'file (current-buffer))

;; check for common errors and args.
(defun monotone-cmd-buf (prefix cmds &optional buf)
  "Run a simple monotone command for this buffer.
PREFIX selects the scope.  CMDS is the command to execute. BUF is
the buffer if not global."
  (setq prefix (monotone-arg-decode prefix)) ;; what is the scope?
  (setq buf (or buf (current-buffer))) ;; default
  (cond
   ;; no args
   ((eq prefix 'global)
    (monotone-cmd cmds))
   ;; path/.
   ((eq prefix 'tree)
    (let ((path (buffer-file-name buf)))
      (when (not path)
        (error "This buffer is not a file"))
      (setq 
       path (concat (file-name-directory (monotone-extract-MT-path path)) "."))
      (monotone-cmd (append cmds (list path)))))
   ;; path/file
   ((eq prefix 'file)
    (let ((name (buffer-file-name buf)))
      (if name
        (monotone-cmd (append cmds (list (monotone-extract-MT-path name))))
        (error "This buffer is not a file"))))
   (t
    (error "Bad prefix"))))

;; runs the command without the buffer.
;;  (let ((bfn (buffer-file-name)))
;;    (when (not bfn)
;;      (error "No file-name for buffer"))
;;    (let* ((monotone-MT-top (monotone-find-MT-top bfn))
;;           (bmn (monotone-extract-MT-path bfn)))
;;

;; NOTE: The command names are modeled after the vc command names.

(defun monotone-set-log-depth (arg)
  "Set the max number of entries displayed in log output to ARG."
  (interactive "NEnter max depth of log entries to report (0=all): ")
  (setq monotone-log-depth arg))
;; (monotone-set-log-depth 10)

(defun monotone-vc-print-log (&optional arg)
  "Print the log for this buffer.  With prefix ARG the global log."
  (interactive "p")
  ;; MONOTONE BUG: when using "log ." the command must be run in that dir.
  ;; monotone.el runs its commands in the top dir so
  ;; just report it for now
  (when (eq 'tree (monotone-arg-decode arg))
    (error "monotone subtree log is busted"))
  ;; 
  (let ((cmds (list "log"))
        (depth monotone-log-depth))
    (when (and (numberp depth) (< 0 depth))
      (setq cmds (append cmds (list (format "--depth=%d" depth)))))
    (monotone-cmd-buf arg cmds)
    (rename-buffer "*monotone log*" t)))
;; (monotone-vc-print-log)

(defun monotone-vc-diff (&optional arg)
  "Print the diffs for this buffer.  With prefix ARG, the global diffs."
  (interactive "p")
  (save-some-buffers)
  (let ((what (monotone-arg-decode arg))
        (name (buffer-file-name)))
    (monotone-cmd-buf what '("diff"))
    (diff-mode)
    (rename-buffer
     (format "*monotone diff %s*"
             (case what
               ('file (monotone-extract-MT-path name))
               (t what))) t)))

(defun monotone-vc-register ()
  "Register this file with monotone for the next commit."
  (interactive)
  (if buffer-file-name
    (monotone-cmd-buf 'file "add" (current-buffer))
    (error "This buffer does not have a file name")))

(defun monotone-vc-status ()
  "Print the status of the current branch."
  (interactive)
  (monotone-cmd "status"))

(defun monotone-vc-update-change-log ()
  "Edit the monotone change log."
  (interactive)
  (let ((mt-top (monotone-find-MT-top)))
    (when (not mt-top)
      (error "Unable to find MT directory"))
    (find-file-other-window (concat mt-top "MT/log"))))

;; (monotone-vc-update-change-log)

(defun monotone-cat-revision ()
  "Display the current revision."
  (interactive)
  (monotone-cmd '("cat" "revision")))

;;;;;;;;;;

(defvar monotone-id-regexp "\\([0-9A-Fa-f]\\{40\\}\\)"
  "A regexp matching a monotone id.")

(defun monotone-id-at-point ()
  "Return the ID under the point."
  (save-excursion
    (skip-chars-backward "0-9A-Fa-f" (- (point) 40))
    (if (looking-at monotone-id-regexp)
      (match-string 1)
      nil)))

(defun monotone-grab-id ()
  "Grab the id under point and put it in the kill buffer for later use.
Grab the ids you want from the buffer and then yank back when needed."
  (interactive)
  (let ((id (monotone-id-at-point)))
    (when (not id)
      (error "Point is not on a monotone id"))
    (setq monotone-last-id id)
    (kill-new id)))

(defun monotone-id-at-point-prompt (what defaultid)
  "Get the id at point.  Prompt for WHAT not found, defaulting to DEFAULTID."
  (let ((id (monotone-id-at-point)))
    (when (not id)
      (let ((prompt (capitalize (format "%s: " what))))
        (setq id (read-string prompt (or defaultid monotone-last-id)))))
    id))
;; (monotone-id-at-point-prompt 'file)

(defun monotone-cat-id (what id)
  "Display the item WHAT which has ID."
  (when id
    (let ((what (format "%s" what))
          (name (format "*monotone %s %s*" what id)))
      (monotone-cmd (list "cat" what id))
      ;; remember it
      (setq monotone-last-id id)
      ;; dont duplicate the buffers
      (if (get-buffer name)
           (kill-buffer name))
      (rename-buffer name))))

(defun monotone-cat-id-pd (what default)
  "A helper function."
  (monotone-cat-id what (or id (monotone-id-at-point-prompt what default))))

(defun monotone-cat-fileid (&optional id)
  "Display the file with ID."
  (interactive)
  (monotone-cat-id-pd 'file monotone-last-fileid)
  (setq monotone-last-fileid monotone-last-id))

(defun monotone-cat-manifestid (&optional id)
  "Display the manifest with ID."
  (interactive)
  (monotone-cat-id-pd 'manifest monotone-last-manifestid)
  (setq monotone-last-revisionid monotone-last-id))

(defun monotone-cat-revisionid (&optional id)
  "Display the revision with ID."
  (interactive)
  (monotone-cat-id-pd 'revision monotone-last-revisionid)
  (setq monotone-last-revisionid monotone-last-id))

;;;;;;;;;;

(defvar monotone-menu
  (let ((map (make-sparse-keymap "Monotone")))
    ;; These need to be in reverse order
    (define-key map [monotone-sync]
      '(menu-item "DB Sync" monotone-sync))
    (define-key map [monotone-push]
      '(menu-item "DB Push" monotone-push))
    (define-key map [monotone-pull]
      '(menu-item "DB Pull" monotone-pull))
    (define-key map [monotone-separator] '("--"))
    ;;
    (define-key map [monotone-cat-rid]
      '(menu-item "Cat this revision id" monotone-cat-revisionid))
    (define-key map [monotone-cat-mid]
      '(menu-item "Cat this manifest id" monotone-cat-manifestid))
    (define-key map [monotone-cat-fid]
      '(menu-item "Cat this file     id" monotone-cat-fileid))
    (define-key map [monotone-separator] '("--"))
    ;;
    (define-key map [monotone-grab-id]
      '(menu-item "Grab ID" monotone-grab-id))
    (define-key map [monotone-vc-status]
      '(menu-item "Status" monotone-vc-status))
    (define-key map [monotone-vc-diff]
      '(menu-item "Diff" monotone-vc-diff))
    (define-key map [monotone-vc-log]
      '(menu-item "Log" monotone-vc-log))
    ;;
    map))

(when monotone-menu-name
  (define-key-after
    (lookup-key global-map [menu-bar])
    [monotone] (cons monotone-menu-name monotone-menu)))

(provide 'monotone)
;;; monotone.el ends here
