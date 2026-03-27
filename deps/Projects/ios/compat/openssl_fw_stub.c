/* openssl_fw_stub.c — 最小源文件，用于创建 openssl.framework
 * 将预编译的 libcrypto.a 重新导出为动态 framework
 * 实际符号来自 target_link_libraries 中的 ${OPENSSL_CRYPTO_LIB}
 */

/* 占位符导出符号，防止 Xcode 生成空 dylib 报错 */
__attribute__((visibility("default")))
int openssl_fw_placeholder(void) { return 1; }
