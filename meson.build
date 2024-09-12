# SPDX-License-Identifier: MIT
project('acews',
    ['c'],
    meson_version: '>=0.60.0',
    version: files('version.txt'),
)
compiler = meson.get_compiler('c')

includes = [
    include_directories('include'),
]
private_includes = [
    include_directories('src'),
]
sources = []
defines = ['-DCONFIG_EWS_LOG_LEVEL_VERBOSE']

subdir('src')

mbedcrypto_dep = dependency('mbedcrypto', required: false)
if not mbedcrypto_dep.found()
    mbedcrypto_dep = compiler.find_library('mbedcrypto',
        required: true,
        has_headers: ['mbedtls' / 'aes.h'],
    )
endif
mbedx509_dep = dependency('mbedx509', required: false)
if not mbedx509_dep.found()
    mbedx509_dep = compiler.find_library('mbedx509',
        required: true,
        has_headers: ['mbedtls' / 'x509.h'],
    )
endif
mbedtls_dep = dependency('mbedtls', required: false)
if not mbedtls_dep.found()
    mbedtls_dep = compiler.find_library('mbedtls',
        required: true,
        has_headers: ['mbedtls' / 'ssl.h'],
    )
endif

depends = [
    dependency('threads'),
    mbedcrypto_dep,
    mbedx509_dep,
    mbedtls_dep,
]

libacews = static_library('libacews',
    sources,
    c_args: defines,
    dependencies: depends,
    include_directories: includes,
)

acews_dep = declare_dependency(
    compile_args: defines,
    include_directories: includes,
    link_with: [libacews],
)

meson.override_dependency('acews', acews_dep)

bin2c_py = find_program('tools' / 'bin2c.py')
build_docs_sh = find_program('tools' / 'build-docs.sh')

docs = custom_target('docs',
    output: 'docs',
    command: [build_docs_sh, meson.current_source_dir() / 'docs', '@OUTPUT@'],
    build_always_stale: true,
)

run_target('browse',
    depends: docs,
    command: ['sensible-browser', 'file://' / docs.full_path() / 'html' / 'index.html'],
)

# silence language server unused variable warnings
bin2c_py = bin2c_py
