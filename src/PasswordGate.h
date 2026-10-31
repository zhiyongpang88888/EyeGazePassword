#pragma once
#include <string>

// 【任务5】密码输入
// demo 只做内存比对，密码写死为 "1234"
// 扩展方向：改存文件/数据库、加盐哈希、支持修改密码等
class PasswordGate {
public:
    static const int kLen = 4; // 固定 4 位密码

    // 追加一位数字（0-9），满了就自动校验
    void inputDigit(char d);
    void clear();

    // 校验结果：0=还没输完 1=正确 2=错误
    int state() const { return state_; }
    const std::string& entered() const { return buf_; }

private:
    bool verify() const;

    std::string buf_;
    std::string password_ = "1234";
    int state_ = 0;
};
