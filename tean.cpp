﻿#include "tean.h"

using std::string;

#include "xrand.h"
#include "swap.h"

TEAN_DATA::TEAN_DATA()
:A(0),B(0)
  {
  }

TEAN_DATA::TEAN_DATA(const TEAN_UL a,const TEAN_UL b)
:A(a),B(b)
  {
  }

TEAN_DATA::TEAN_DATA(const void* datas)
  {
  memcpy(Data,datas,sizeof(Data));
  }

TEAN_DATA::operator string()
  {
  return string((const char*)Data, sizeof(Data));
  }


TEAN_KEY::TEAN_KEY()
:K1(0),K2(0),K3(0),K4(0)
  {
  }

TEAN_KEY::TEAN_KEY(const TEAN_UL k1,
                   const TEAN_UL k2,
                   const TEAN_UL k3,
                   const TEAN_UL k4)
:K1(k1),K2(k2),K3(k3),K4(k4)
  {
  }

TEAN_KEY::TEAN_KEY(const void* key)
  {
  memcpy(Key,key,sizeof(Key));
  }

//! 魔术常量 == 实数部分(2^32 * 黄金比例ψ)。ψ == sqrt(5/4) - 1/2 ≈ 0.618034;
static const TEAN_UL gk_tean_delta = 0x9E3779B9;
static const TEAN_UL gk_xtean_delta = 0x57E89147;

//! TEA加密轮数，16轮，一般32轮，推荐64轮
static const size_t gk_tean_round = 0x10;
static const size_t gk_xtean_round = 0x20;

//! 一个分组数据的大小，以byte计，共8 byte
static const size_t gk_tea_data_size = sizeof(TEAN_DATA);

//! 算法用于大尾机器，小尾机器上需要翻转
static const short gk_tea_4210 = 0x4210;

//! 这个常数如果做成static const bool无法被编译器优化
#define gk_tean_need_bswap ((*(const char* const)&gk_tea_4210) == 0x10)

TEAN_DATA TeaEncipher(const TEAN_DATA&    encrypt_data,
                      const TEAN_KEY&     key,
                      const TEAN_UL       delta,
                      const size_t        xtea_round)
  {
  TEAN_DATA data(
    (gk_tean_need_bswap ? bswap(encrypt_data.A) : encrypt_data.A),
    (gk_tean_need_bswap ? bswap(encrypt_data.B) : encrypt_data.B));

  TEAN_UL sum = delta;
  for(size_t i = 0; i < xtea_round; ++i)
    {
    data.A += ((data.B << 4) + key.K1) ^
      (data.B + sum) ^
      ((data.B >> 5) + key.K2);
    data.B += ((data.A << 4) + key.K3) ^
      (data.A + sum) ^
      ((data.A >> 5) + key.K4);
    sum += delta;
    }

  return TEAN_DATA(
    (gk_tean_need_bswap ? bswap(data.A) : data.A),
    (gk_tean_need_bswap ? bswap(data.B) : data.B));
  }

TEAN_DATA TeanEncipher(const TEAN_DATA&   encrypt_data,
                       const TEAN_KEY&    key)
  {
  return TeaEncipher(encrypt_data, key, gk_tean_delta, gk_tean_round);
  }

static TEAN_DATA TeaEncipherRound(TEAN_DATA&        data,
                                  TEAN_DATA&        pre_data,
                                  const TEAN_KEY&   key,
                                  TEAN_DATA&        cipher,
                                  const TEAN_UL     delta,
                                  const size_t      xtea_round)
  {
  data.A ^= cipher.A;
  data.B ^= cipher.B;     //当前数据与上组密文xor
  cipher = TeaEncipher(data, key, delta, xtea_round);
  cipher.A ^= pre_data.A;
  cipher.B ^= pre_data.B; //当前密文与上组数据xor
  pre_data = data;        //保存当前数据
  return cipher;
  }

string TeaEncrypt(const void*        encrypt_data,
                  const size_t       encrypt_data_size,
                  const TEAN_KEY&    encrypt_key,
                  const TEAN_UL      delta,
                  const size_t       xtea_round)
  {
  string rets;

  if(encrypt_data == nullptr || (intptr_t)encrypt_data_size <= 0)
     return rets;

  const TEAN_KEY key(
    (gk_tean_need_bswap ? bswap(encrypt_key.K1) : encrypt_key.K1),
    (gk_tean_need_bswap ? bswap(encrypt_key.K2) : encrypt_key.K2),
    (gk_tean_need_bswap ? bswap(encrypt_key.K3) : encrypt_key.K3),
    (gk_tean_need_bswap ? bswap(encrypt_key.K4) : encrypt_key.K4));

  TEAN_DATA tail(0, 0);        //数据尾，初始为全0

  const unsigned char* lp_encrypt = (const unsigned char*)encrypt_data;

  //数据最后一个字节与7个00成组
  tail.Data[0] = lp_encrypt[encrypt_data_size - 1];

  //补充填充数据个数，0~7
  const size_t padding_num = (gk_tea_data_size - ((encrypt_data_size + 1 + 2 + 7)
    % gk_tea_data_size)) % gk_tea_data_size;

  //加密轮数，最少2轮
  const size_t round = (encrypt_data_size + 0xA + padding_num) / 8;

  TEAN_DATA header[2];        //填充数据头，1或2组
  //随机数填充数据头
  for(size_t i = 0; i < gk_tea_data_size*2; ++i)
    {
    header[i/gk_tea_data_size].Data[i%gk_tea_data_size] = xrand() & 0xFF;
    }

  //第一字节写入填充数据个数，3bit
  header[0].Data[0] &= 0xF8;
  header[0].Data[0] |= padding_num;

  //填充数据与正式数据成组，可能情况下会多读取一组正式数据或读取一组无效数据
  for(size_t i = 3 + padding_num; i < gk_tea_data_size * 2; ++i)
    {
    header[i / gk_tea_data_size].Data[i%gk_tea_data_size] = *lp_encrypt;
    ++lp_encrypt;
    }

  TEAN_DATA data(0,0);        //用于写入当前轮加密数据
  TEAN_DATA pre_data(data);   //用于保存上轮加密数据
  TEAN_DATA cipher(data);     //用于保存上轮密文，注意初始化为0保证数据头计算正确

  for(size_t i = 1; i <= round; ++i)
    {
    if(i == round)    //最后一轮加密时使用数据尾，注意round可能为2，所以判断必须在前
      {
      data = tail;
      }
    else
      {
      if(i <= 2)      //轮数大于2时，头两轮使用数据头，轮数大于1时，首轮使用数据头
        {
        data = header[i-1];
        }
      else           //其余轮使用正式数据本身
        {
        data = TEAN_DATA(lp_encrypt);
        lp_encrypt += gk_tea_data_size;
        }
      }
    const TEAN_DATA TD(TeaEncipherRound(data, pre_data, key, cipher, delta, xtea_round));
    rets.append((const char*)TD.Data,sizeof(TD.Data));
    }

  return rets;
  }

string TeanEncrypt(const void*       encrypt_data,
                   const size_t      encrypt_data_size,
                   const TEAN_KEY&   encrypt_key)
  {
  return TeaEncrypt(encrypt_data, encrypt_data_size, encrypt_key, gk_tean_delta, gk_tean_round);
  }

TEAN_DATA TeaDecipher(const TEAN_DATA&    decrypt_data,
                      const TEAN_KEY&     key,
                      const TEAN_UL       delta,
                      const size_t        xtea_round)
  {
  TEAN_DATA data(
    (gk_tean_need_bswap ? bswap(decrypt_data.A) : decrypt_data.A),
    (gk_tean_need_bswap ? bswap(decrypt_data.B) : decrypt_data.B));

#pragma warning(push)
#pragma warning(disable:4307)   //C4307: “*”: 整型常量溢出
  TEAN_UL sum = delta * (TEAN_UL)xtea_round;
#pragma warning(pop)

  for(size_t i = 0; i < xtea_round; ++i)
    {
    data.B -= ((data.A << 4) + key.K3) ^
      (data.A + sum) ^
      ((data.A >> 5) + key.K4);
    data.A -= ((data.B << 4) + key.K1) ^
      (data.B + sum) ^
      ((data.B >> 5) + key.K2);
    sum -= delta;
    }

  return TEAN_DATA(
    (gk_tean_need_bswap ? bswap(data.A) : data.A),
    (gk_tean_need_bswap ? bswap(data.B) : data.B));
  }

TEAN_DATA TeanDecipher(const TEAN_DATA&   decrypt_data,
                       const TEAN_KEY&    key)
  {
  return TeaDecipher(decrypt_data, key, gk_tean_delta, gk_tean_round);
  }

//! 轮解密，返回解密结果
static TEAN_DATA TeaDecipherRound(TEAN_DATA&        cipher,
                                  TEAN_DATA&        pre_cipher,
                                  const TEAN_KEY&   key,
                                  TEAN_DATA&        pre_data,
                                  const TEAN_UL     delta,
                                  const size_t      xtea_round)
  {
  pre_data.A ^= cipher.A;
  pre_data.B ^= cipher.B;       //当前密文与上轮解密数据异或
  pre_data = TeaDecipher(pre_data, key, delta, xtea_round);

  TEAN_DATA data(
    pre_data.A ^ pre_cipher.A,
    pre_data.B ^ pre_cipher.B
    );  //明文由解密后数据与上轮密文异或得到
  //注意，pre_data保留解密数据，不是保留明文
  pre_cipher = cipher;          //保存当前密文用于下轮运算

  return data;
  }

string TeaDecrypt(const void*        decrypt_data,
                  const size_t       decrypt_data_size,
                  const TEAN_KEY&    decrypt_key,
                  const TEAN_UL      delta,
                  const size_t       xtea_round)
  {
  string rets;

  //解密串必须>=16并且为8倍数
  if(decrypt_data == nullptr||
     (intptr_t)decrypt_data_size < (intptr_t)(2 * gk_tea_data_size) ||
    (decrypt_data_size % gk_tea_data_size))
    return rets;

  const TEAN_KEY key(
    (gk_tean_need_bswap ? bswap(decrypt_key.K1) : decrypt_key.K1),
    (gk_tean_need_bswap ? bswap(decrypt_key.K2) : decrypt_key.K2),
    (gk_tean_need_bswap ? bswap(decrypt_key.K3) : decrypt_key.K3),
    (gk_tean_need_bswap ? bswap(decrypt_key.K4) : decrypt_key.K4));

  TEAN_DATA pre_cipher(0,0);   //用于保存上轮解密前数据
  TEAN_DATA pre_data(0,0);     //用于保存上轮解密后数据

  const unsigned char* lp_decrypt = (const unsigned char*)decrypt_data;

  for(size_t i = 0;i < (decrypt_data_size / gk_tea_data_size);++i)
    {
    TEAN_DATA cipher(&lp_decrypt[i*gk_tea_data_size]);
    const TEAN_DATA TD(TeaDecipherRound(cipher, pre_cipher, key, pre_data, delta, xtea_round));
    rets.append((const char*)TD.Data,sizeof(TD.Data));
    }

  //需要验证结尾0，并去除结尾7 byte 0
  for(size_t i = 0; i < 7; ++i)
    {
    if(*(rets.rbegin()) != '\x00')
      {
      rets.clear();
      return rets;
      }
    rets.pop_back();
    }

  const size_t padding_num = ((*(rets.c_str())) & 7) + 3;
  //得到数据填充数据大小，当总填充数超过剩余数据大小时，出错
  if(padding_num >= rets.size())
    {
    rets.clear();
    return rets;
    }
  rets.erase(0,padding_num);  //丢弃填充数据头

  //最后一次确认填充数
  const size_t chk_padding_num =
     (gk_tea_data_size - ((rets.size() + 1 + 2 + 7) % gk_tea_data_size)) %
     gk_tea_data_size + 3;
  if(padding_num != chk_padding_num)
    {
    rets.clear();
    return rets;
    }

  return rets;
  }

string TeanDecrypt(const void*       decrypt_data,
                   const size_t      decrypt_data_size,
                   const TEAN_KEY&   decrypt_key)
  {
  return TeaDecrypt(decrypt_data, decrypt_data_size, decrypt_key, gk_tean_delta, gk_tean_round);
  }


string XTeanEncrypt(const void*        encrypt_data,
                    const size_t       encrypt_data_size,
                    const TEAN_KEY&    encrypt_key)
  {
  return TeaEncrypt(encrypt_data, encrypt_data_size, encrypt_key, gk_xtean_delta, gk_xtean_round);
  }

string XTeanDecrypt(const void*        decrypt_data,
                    const size_t       decrypt_data_size,
                    const TEAN_KEY&    decrypt_key)
  {
  return TeaDecrypt(decrypt_data, decrypt_data_size, decrypt_key, gk_xtean_delta, gk_xtean_round);
  }


std::string XxTeaEncrypt(const void*        encrypt_data,
                         const size_t       encrypt_data_size,
                         const TEAN_KEY&    encrypt_key)
  {
  std::string rets((const char*)encrypt_data, encrypt_data_size);
  XXTEA_DATA* data = (XXTEA_DATA*)rets.c_str();
  const size_t block_count = encrypt_data_size / sizeof(XXTEA_DATA);
  if(block_count == 0)  return rets;
  intptr_t rounds = 6 + 52 / block_count;

  XXTEA_DATA sum = 0;

  XXTEA_DATA z = data[block_count - 1];
  XXTEA_DATA y;
  while(rounds-- > 0)
    {
    sum += gk_tean_delta;
    const XXTEA_DATA e = (sum >> 2) & 3;
    for(size_t i = 0; i < block_count - 1; ++i)
      {
      const XXTEA_DATA k = *(&(encrypt_key.K1) + ((i & 3) ^ e));
      y = data[i + 1];
      data[i] += ((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4)) ^
        (sum ^ y) + (k ^ z);
      z = data[i];
      }
    const XXTEA_DATA k = *(&(encrypt_key.K1) + (((block_count - 1) & 3) ^ e));
    y = data[0];
    data[block_count - 1] += ((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4)) ^
      (sum ^ y) + (k ^ z);
    z = data[block_count - 1];
    }
  return rets;
  }

std::string XxTeaDecrypt(const void*        decrypt_data,
                         const size_t       decrypt_data_size,
                         const TEAN_KEY&    decrypt_key)
  {
  std::string rets((const char*)decrypt_data, decrypt_data_size);
  XXTEA_DATA* data = (XXTEA_DATA*)rets.c_str();
  const size_t block_count = decrypt_data_size / sizeof(XXTEA_DATA);
  if(block_count == 0)  return rets;
  const size_t rounds = 6 + 52 / block_count;

  XXTEA_DATA sum = (XXTEA_DATA)(rounds * gk_tean_delta);

  XXTEA_DATA z;
  XXTEA_DATA y = data[0];
  while(sum != 0)
    {
    const XXTEA_DATA e = (sum >> 2) & 3;
    for(auto i = block_count - 1; i > 0; --i)
      {
      const XXTEA_DATA k = *(&(decrypt_key.K1) + ((i & 3) ^ e));
      z = data[i - 1];
      data[i] -= ((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4)) ^
        (sum ^ y) + (k ^ z);
      y = data[i];
      }
    const XXTEA_DATA k = *(&(decrypt_key.K1) + e);
    z = data[block_count - 1];
    data[0] -= ((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4)) ^
      (sum ^ y) + (k ^ z);
    y = data[0];
    sum -= gk_tean_delta;
    }
  return rets;
  }