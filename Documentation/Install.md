
# Installation

*You can install this module via `dkms` .*

<br>

## ![Badge Ubuntu]

```
sudo apt install                \
    linux-headers-$(uname -r)   \
    build-essential             \
    dkms                        \
    git

cd ~

git clone https://github.com/ocerman/zenpower.git

cd zenpower

sudo make dkms-install
```

<br>


<!----------------------------------------------------------------------------->

[Badge Ubuntu]: https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge