version_info = (0, 0, 1)

if not version_info[-1]:
    version = ".".join([str(num) for num in version_info[:-1]])
else:
    version = ".".join([str(num) for num in version_info])
