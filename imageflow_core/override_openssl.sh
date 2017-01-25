#!/bin/bash
set -e


export OPENSSL_LIB_DIR="$(grep  -o -m 1 '/[^"]*/OpenSSL/1.0.2j/[^"]*' ./conan_cargo_build.rs)"
export OPENSSL_DIR="$(dirname "$OPENSSL_LIB_DIR")"
export DEP_OPENSSL_VERSION="102"

mkdir ../.cargo || true

printf "#autogenerated\n\n" > ../.cargo/config

for i in $(rustc --print target-list)
do 
	{ printf '[target.%s.openssl]\n' "$i";
	  printf 'rustc-link-search = ["%s"]\n' "$OPENSSL_LIB_DIR" ;
	  printf 'include = "%s"\n' "$OPENSSL_LIB_DIR/include" ;
	  printf 'rustc-link-lib = ["ssl", "crypto"]\n'; #rustc-cfg = "ossl102"\nrustc-cfg = "ossl10x"
	  printf 'rustc-cfg=["ossl102"]\nversion = "102"\nconf = ""\n\n\n';
	 } >> ../.cargo/config 
done
