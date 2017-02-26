# Monotone repo converted to git with mtn git_export

## 1. clone monotone repo

```
 $ mtn --db=export.db clone "mtn://monotone.ca/monotone?net.venge.monotone*" -b net.venge.monotone export
```

## 2. get authors

```
 $ mtn --db=export.db db execute 'select distinct value from revision_certs where name = "author"' >> export-authors
```

## 3. get committers

```
 $ mtn --db=export.db db execute \
       'select distinct public_keys.name
        from public_keys
        left join revision_certs on revision_certs.keypair_id = public_keys.id
        where revision_certs.name = "author"' >> export-authors
```

## 4. review logs from net.venge.montone branch & update export-authors file

add details from AUTHORS file and convert into key = value format for export

## 5. initial monotone export / git import

```
 $ mkdir export.git
 $ cd export.git
 $ git init
 $ mtn --db=../export.db --authors-file=../export-authors git_export | git fast-import
```

fails git fsck on bad date certs from 1969

```
 $ git fsck
   error in commit b84014569ca26ad7f4564085e33f5b7f1fbd6221: badDateOverflow: invalid author/committer line - date causes integer overflow
   error in commit 77464deefb7ee30b6bffb3c940fe08f6dbe0b0ce: badDateOverflow: invalid author/committer line - date causes integer overflow
   error in commit 826c7de236db165b066b64b3bccc8bd738601abd: badDateOverflow: invalid author/committer line - date causes integer overflow
   error in commit 20e44d124f2067cefad8b10cd41ef76eb2840c9f: badDateOverflow: invalid author/committer line - date causes integer overflow
   error in commit e2f17bde065f5d2d399c7cb098ebffdae7973f47: badDateOverflow: invalid author/committer line - date causes integer overflow
```

## 6. second monotone export / git import

add `--log-revs` and `--log-certs` to find monotone revisions with bad date certs

```
 $ mtn --db=../export.db --authors-file=../export-authors --log-revs --log-certs git_export | git fast-import
 $ git fsck
 $ git show ...
```

## 7. remove date certs from 1969 on from selected monotone revisions

```
   mtn --db=export.db local kill_certs 00bc6370f21302d2bd7f2008f4608d3a2d5683d0 date
   mtn --db=export.db local kill_certs 57f949b1c893e4a786eae11a54af2e72d8b90002 date
   mtn --db=export.db local kill_certs 8cd9e68550663519d302fe19231eda088de8a053 date
   mtn --db=export.db local kill_certs 9c4a4ec5be257e4320ced76a0321760648b8e50c date
   mtn --db=export.db local kill_certs cdf1680ec496d22f863184a1fae75ef43bbbe6fc date
```

## 8. final monotone export / git import

```
 $ mkdir export.git
 $ cd export.git
 $ git init
 $ mtn --db=../export.db --authors-file=../export-authors git_export | git fast-import

 $ git fsck
   Checking object directories: 100% (256/256), done.
   Checking objects: 100% (90198/90198), done.
```

## 9. create master branch and add this readme
