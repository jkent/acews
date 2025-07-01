# SPDX-License-Identifier: MIT
project('acews', ['c'],
    meson_version: '>=0.60.0',
    version: files('version.txt'),
)
compiler = meson.get_compiler('c')

use_ipv6 = get_option('use-ipv6')
use_tls = get_option('use-tls')
use_ws = get_option('use-websockets')
default_timeout = get_option('default-timeout')
default_backlog = get_option('default-backlog')
session_bufsize = get_option('session-bufsize')
worker_stack = get_option('worker-stack')
ws_stack = get_option('websocket-stack')
http_clients = get_option('http-clients')
ws_clients = get_option('ws-clients')
https_clients = get_option('https-clients')
wss_clients = get_option('wss-clients')
log_level = get_option('log-level')

conf_data = configuration_data()
conf_data.set10('USE_IPV6', use_ipv6)
conf_data.set10('USE_TLS', use_tls)
conf_data.set10('USE_WEBSOCKET', use_ws)

conf_data.set('DEFAULT_BACKLOG', default_backlog)
conf_data.set('DEFAULT_TIMEOUT', default_timeout)
conf_data.set('SESSION_BUFSIZE', session_bufsize)
conf_data.set('WORKER_STACK', worker_stack)
conf_data.set('WS_STACK', ws_stack)
conf_data.set('HTTP_CLIENTS', http_clients)
conf_data.set('WS_CLIENTS', ws_clients)
conf_data.set('HTTPS_CLIENTS', https_clients)
conf_data.set('WSS_CLIENTS', wss_clients)
if log_level == 'none'
    conf_data.set('LOG_LEVEL', 'LOG_NONE')
elif log_level == 'fatal'
    conf_data.set('LOG_LEVEL', 'LOG_FATAL')
elif log_level == 'error'
    conf_data.set('LOG_LEVEL', 'LOG_ERROR')
elif log_level == 'warning'
    conf_data.set('LOG_LEVEL', 'LOG_WARN')
elif log_level == 'info'
    conf_data.set('LOG_LEVEL', 'LOG_INFO')
elif log_level == 'debug'
    conf_data.set('LOG_LEVEL', 'LOG_DEBUG')
elif log_level == 'verbose'
    conf_data.set('LOG_LEVEL', 'LOG_VERBOSE')
endif

configure_file(
    input: 'include/ews_config.h.in',
    output: 'ews_config.h',
    configuration: conf_data,
)

includes = [include_directories(
    '.',
    'include'
)]
private_includes = [
    include_directories('src'),
]
sources = []

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
    dependencies: depends,
    include_directories: includes,
)

acews_dep = declare_dependency(
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
