# @X

Atx is a template that make you a webserver with **( API,CSR,SSR,PR )** at the same time with small amount of code
## :scroll: Documentation

[read th readme refrence](https://github.com/silitonix/atx/tree/main/doc)


## :construction: Building

Project using **meson** for build project you need to install meson

```bash
meson setup build
meson compile -C build/
```

For build runtime use gcc to create a shared file. **file name is changable.**
```bash
gcc -shared handle.c -o runtime.btx
```
## :memo: To do

## :pencil2: Authors

- [@silitonix](https://www.github.com/silitonix)
- [@mr_mn2](https://www.github.com/mr_mn2)

## License

[MIT](https://choosealicense.com/licenses/mit/)
