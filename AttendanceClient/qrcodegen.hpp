#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace qrcodegen {
class QrSegment final {
	public: class Mode final {
		public: static const Mode NUMERIC; // 数字模式
		public: static const Mode ALPHANUMERIC; // 字母数字模式
		public: static const Mode BYTE; // 字节模式
		public: static const Mode KANJI; // 汉字模式
		public: static const Mode ECI; // 扩展字符模式
		private: int modeBits; // 模式标识位
		private: int numBitsCharCount[3]; // 不同版本的字符计数位数
		private: Mode(int mode, int cc0, int cc1, int cc2); // 内部构造函数
		public: int getModeBits() const; // 获取模式标识位
		public: int numCharCountBits(int ver) const; // 获取指定版本的字符计数位数
	};
	public: static QrSegment makeBytes(const std::vector<std::uint8_t> &data); // 创建字节段
	public: static QrSegment makeNumeric(const char *digits); // 创建数字段
	public: static QrSegment makeAlphanumeric(const char *text); // 创建字母数字段
	public: static std::vector<QrSegment> makeSegments(const char *text); // 创建文本分段
	public: static QrSegment makeEci(long assignVal); // 创建 ECI 段
	public: static bool isNumeric(const char *text); // 判断是否为纯数字
	public: static bool isAlphanumeric(const char *text); // 判断是否为字母数字
	private: const Mode *mode; // 当前编码模式
	private: int numChars; // 字符数量
	private: std::vector<bool> data; // 段数据位
	public: QrSegment(const Mode &md, int numCh, const std::vector<bool> &dt); // 构造分段对象
	public: QrSegment(const Mode &md, int numCh, std::vector<bool> &&dt); // 构造分段对象
	public: const Mode &getMode() const; // 获取编码模式
	public: int getNumChars() const; // 获取字符数量
	public: const std::vector<bool> &getData() const; // 获取数据位
	public: static int getTotalBits(const std::vector<QrSegment> &segs, int version); // 统计总位数
	private: static const char *ALPHANUMERIC_CHARSET; // 字母数字字符集
};
class QrCode final {
	public: enum class Ecc {
		LOW = 0 , // 低容错等级
		MEDIUM  , // 中容错等级
		QUARTILE, // 较高容错等级
		HIGH    , // 高容错等级
	};
	private: static int getFormatBits(Ecc ecl); // 获取格式信息位
	public: static QrCode encodeText(const char *text, Ecc ecl); // 编码文本二维码
	public: static QrCode encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl); // 编码二进制二维码
	public: static QrCode encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
	int minVersion=1, int maxVersion=40, int mask=-1, bool boostEcl=true); // 编码分段二维码
	private: int version; // 二维码版本
	private: int size; // 二维码尺寸
	private: Ecc errorCorrectionLevel; // 纠错等级
	private: int mask; // 掩码编号
	private: std::vector<std::vector<bool> > modules; // 模块矩阵
	private: std::vector<std::vector<bool> > isFunction; // 功能模块标记
	public: QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int msk); // 构造二维码对象
	public: int getVersion() const; // 获取版本号
	public: int getSize() const; // 获取尺寸
	public: Ecc getErrorCorrectionLevel() const; // 获取纠错等级
	public: int getMask() const; // 获取掩码编号
	public: bool getModule(int x, int y) const; // 获取指定模块状态
	private: void drawFunctionPatterns(); // 绘制功能图案
	private: void drawFormatBits(int msk); // 绘制格式信息
	private: void drawVersion(); // 绘制版本信息
	private: void drawFinderPattern(int x, int y); // 绘制定位图案
	private: void drawAlignmentPattern(int x, int y); // 绘制对齐图案
	private: void setFunctionModule(int x, int y, bool isDark); // 设置功能模块
	private: bool module(int x, int y) const; // 读取模块值
	private: std::vector<std::uint8_t> addEccAndInterleave(const std::vector<std::uint8_t> &data) const; // 添加纠错并交错
	private: void drawCodewords(const std::vector<std::uint8_t> &data); // 绘制码字数据
	private: void applyMask(int msk); // 应用掩码
	private: long getPenaltyScore() const; // 计算惩罚分数
	private: std::vector<int> getAlignmentPatternPositions() const; // 获取对齐图案位置
	private: static int getNumRawDataModules(int ver); // 获取原始数据模块数
	private: static int getNumDataCodewords(int ver, Ecc ecl); // 获取数据码字数
	private: static std::vector<std::uint8_t> reedSolomonComputeDivisor(int degree); // 计算里德所罗门除数
	private: static std::vector<std::uint8_t> reedSolomonComputeRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &divisor); // 计算里德所罗门余数
	private: static std::uint8_t reedSolomonMultiply(std::uint8_t x, std::uint8_t y); // 里德所罗门乘法
	private: int finderPenaltyCountPatterns(const std::array<int,7> &runHistory) const; // 统计定位图案惩罚
	private: int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int,7> &runHistory) const; // 终止统计并计分
	private: void finderPenaltyAddHistory(int currentRunLength, std::array<int,7> &runHistory) const; // 追加运行历史
	private: static bool getBit(long x, int i); // 读取指定比特位
	public: static constexpr int MIN_VERSION =  1; // 最小版本
	public: static constexpr int MAX_VERSION = 40; // 最大版本
	private: static const int PENALTY_N1; // 惩罚规则一
	private: static const int PENALTY_N2; // 惩罚规则二
	private: static const int PENALTY_N3; // 惩罚规则三
	private: static const int PENALTY_N4; // 惩罚规则四
	private: static const std::int8_t ECC_CODEWORDS_PER_BLOCK[4][41]; // 每块纠错码字表
	private: static const std::int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41]; // 纠错块数量表
};
class data_too_long : public std::length_error { // 数据过长异常
	public: explicit data_too_long(const std::string &msg); // 异常构造函数
};
class BitBuffer final : public std::vector<bool> { // 位缓冲区
	public: BitBuffer(); // 默认构造函数
	public: void appendBits(std::uint32_t val, int len); // 追加指定长度的比特
};
}
