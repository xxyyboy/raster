[![Build Status](https://travis-ci.org/Yeolar/raster.svg?branch=master)](https://travis-ci.org/Yeolar/raster)

raster
======

raster是一个完整的高性能C++协程服务框架。

该框架借鉴了Facebook的folly基础库的一些思想和内容，但更加注重于轻量、易用、扩展性，同时，已经支持了监控统计、存储、数据库等实用需求。

框架主要特性：

- 支持异步，协程
- 支持多个后端并发请求
- 支持binary、thrift、pbrpc协议
- 支持并行计算
- 支持监控统计、存储、数据库等扩展功能

框架的开发初衷是可以快速完成C++服务的开发，目前用于 [rddoc.com](https://www.rddoc.com/) 的搜索、持久化KV存储、代理服务等。

依赖包括 Boost CURL GFlags ICU OpenSSL Protobuf ZLIB。

编译安装

    $ mkdir build && cd build
    $ make -j8
    $ sudo make install

运行demo

    $ ./examples/empty/empty -conf ../examples/empty/server.json
    $ ./examples/empty/empty-bench -count 1000

详细的介绍和使用方法请参考 [Raster](https://www.rddoc.com/doc/Raster-1.0.0/) 文档。
