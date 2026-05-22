from setuptools import setup, find_packages

setup(
    name="fastkv",
    version="0.1.0",
    description="FastKV — Python binding via CFFI",
    packages=find_packages(),
    setup_requires=["cffi>=1.0.0"],
    install_requires=["cffi>=1.0.0"],
    cffi_modules=["fastkv_ffi_build.py:ffi"],
    python_requires=">=3.10",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX",
    ],
)
