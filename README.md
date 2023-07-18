# Fansub

### Md5sum
```bash
find . -type f -exec md5sum {} > md5sums.txt \;
```
```bash
md5sum -c md5sums.txt
```
<hr>

### Rename with crc
```bash
value=$(crc32 "NameFile") && up=$(echo $value | tr a-z A-Z) && mv "NameFile" "NameFile [${up}].mkv"
```
