// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <boost/variant.hpp>
#include <boost/functional/hash/hash.hpp>
#include <vector>
#include <cstring>  // memcmp
#include <sstream>
#include "serialization/crypto.h"
#include "serialization/serialization.h"
#include "serialization/variant.h"
#include "serialization/binary_archive.h"
#include "serialization/json_archive.h"
#include "serialization/debug_archive.h"
#include "serialization/keyvalue_serialization.h" // epee key-value serialization
#include "string_tools.h"
#include "currency_config.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "misc_language.h"
#include "tx_extra.h"
#include "block_flags.h"

namespace currency
{

  const static crypto::hash null_hash = AUTO_VAL_INIT(null_hash);
  const static crypto::public_key null_pkey = AUTO_VAL_INIT(null_pkey);
  const static crypto::secret_key null_skey = AUTO_VAL_INIT(null_skey);
  const static crypto::signature null_sig = AUTO_VAL_INIT(null_sig);

  typedef std::vector<crypto::signature> ring_signature;

  /* outputs */
  struct txout_to_script
  {
    std::vector<crypto::public_key> keys;
    std::vector<uint8_t> script;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(keys)
      FIELD(script)
    END_SERIALIZE()
  };

  struct txout_to_scripthash
  {
    crypto::hash hash;
  };

  #pragma pack(push, 1)
  struct txout_to_key
  {
    txout_to_key() { }
    txout_to_key(const crypto::public_key &_key) : key(_key) { }

    crypto::public_key key;
    uint8_t mix_attr;
  };
  #pragma pack(pop)
  /* inputs */

  struct txin_gen
  {
    size_t height;

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD(height)
    END_SERIALIZE()
  };

  struct txin_to_script
  {
    crypto::hash prev;
    size_t prevout;
    std::vector<uint8_t> sigset;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(prev)
      VARINT_FIELD(prevout)
      FIELD(sigset)
    END_SERIALIZE()
  };

  struct txin_to_scripthash
  {
    crypto::hash prev;
    size_t prevout;
    txout_to_script script;
    std::vector<uint8_t> sigset;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(prev)
      VARINT_FIELD(prevout)
      FIELD(script)
      FIELD(sigset)
    END_SERIALIZE()
  };

  struct txin_to_key
  {
    uint64_t amount;
    std::vector<uint64_t> key_offsets;
    crypto::key_image k_image;      // double spending protection

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD(amount)
      FIELD(key_offsets)
      FIELD(k_image)
    END_SERIALIZE()
  };


  typedef boost::variant<txin_gen, txin_to_script, txin_to_scripthash, txin_to_key> txin_v;

  typedef boost::variant<txout_to_script, txout_to_scripthash, txout_to_key> txout_target_v;

  //typedef std::pair<uint64_t, txout> out_t;
  struct tx_out
  {
    uint64_t amount;
    txout_target_v target;

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD(amount)
      FIELD(target)
    END_SERIALIZE()
  };

  class transaction_prefix
  {

  public:
    // tx information
    size_t   version;
    uint64_t unlock_time;  //number of block (or time), used as a limitation like: spend this tx not early then block/time

    std::vector<txin_v> vin;
    std::vector<tx_out> vout;
    //extra
    std::vector<uint8_t> extra;

    BEGIN_SERIALIZE()
      VARINT_FIELD(version)
      if(CURRENT_TRANSACTION_VERSION < version) return false;
      VARINT_FIELD(unlock_time)
      FIELD(vin)
      FIELD(vout)
      FIELD(extra)
    END_SERIALIZE()

  protected:
    transaction_prefix(){}
  };

  class transaction: public transaction_prefix
  {
  public:
    std::vector<std::vector<crypto::signature> > signatures; //count signatures  always the same as inputs count

    transaction();
    virtual ~transaction();
    void set_null();

    BEGIN_SERIALIZE_OBJECT()
      FIELDS(*static_cast<transaction_prefix *>(this))
      FIELD(signatures)
    END_SERIALIZE()


    static size_t get_signature_size(const txin_v& tx_in);
    
  };


  inline
  transaction::transaction()
  {
    set_null();
  }

  inline
  transaction::~transaction()
  {
    //set_null();
  }

  inline
  void transaction::set_null()
  {
    version = 0;
    unlock_time = 0;
    vin.clear();
    vout.clear();
    extra.clear();
    signatures.clear();
  }

  inline
  size_t transaction::get_signature_size(const txin_v& tx_in)
  {
    struct txin_signature_size_visitor : public boost::static_visitor<size_t>
    {
      size_t operator()(const txin_gen& /*txin*/) const{return 0;}
      size_t operator()(const txin_to_script& /*txin*/) const{return 0;}
      size_t operator()(const txin_to_scripthash& /*txin*/) const{return 0;}
      size_t operator()(const txin_to_key& txin) const {return txin.key_offsets.size();}
    };

    return boost::apply_visitor(txin_signature_size_visitor(), tx_in);
  }



  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct block_header
  {
    uint8_t major_version;
    uint8_t minor_version;
    uint64_t timestamp;
    crypto::hash  prev_id;
    uint64_t nonce;
    uint8_t flags;

    BEGIN_SERIALIZE()
      FIELD(major_version)
      if(major_version > CURRENT_BLOCK_MAJOR_VERSION) return false;
      FIELD(nonce)
      FIELD(prev_id)
      VARINT_FIELD(minor_version)
      VARINT_FIELD(timestamp)
      FIELD(flags)
    END_SERIALIZE()
  };

  struct block: public block_header
  {
    transaction miner_tx;
    std::vector<crypto::hash> tx_hashes;

    BEGIN_SERIALIZE_OBJECT()
      FIELDS(*static_cast<block_header *>(this))
      FIELD(miner_tx)
      FIELD(tx_hashes)
    END_SERIALIZE()
  };


  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct account_public_address
  {
    crypto::public_key m_spend_public_key;
    crypto::public_key m_view_public_key;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(m_spend_public_key)
      FIELD(m_view_public_key)
    END_SERIALIZE()

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(m_spend_public_key)
      KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(m_view_public_key)
    END_KV_SERIALIZE_MAP()
  };

  struct keypair
  {
    crypto::public_key pub;
    crypto::secret_key sec;

    static inline keypair generate()
    {
      keypair k;
      generate_keys(k.pub, k.sec);
      return k;
    }

    BEGIN_SERIALIZE_OBJECT()
      FIELD(pub)
      FIELD(sec)
    END_SERIALIZE()
  };
  //---------------------------------------------------------------

  typedef std::string payment_id_t;

}

BLOB_SERIALIZER(currency::txout_to_key);
BLOB_SERIALIZER(currency::txout_to_scripthash);

VARIANT_TAG(binary_archive, currency::txin_gen, 0xff);
VARIANT_TAG(binary_archive, currency::txin_to_script, 0x0);
VARIANT_TAG(binary_archive, currency::txin_to_scripthash, 0x1);
VARIANT_TAG(binary_archive, currency::txin_to_key, 0x2);
VARIANT_TAG(binary_archive, currency::txout_to_script, 0x0);
VARIANT_TAG(binary_archive, currency::txout_to_scripthash, 0x1);
VARIANT_TAG(binary_archive, currency::txout_to_key, 0x2);
VARIANT_TAG(binary_archive, currency::transaction, 0xcc);
VARIANT_TAG(binary_archive, currency::block, 0xbb);

VARIANT_TAG(json_archive, currency::txin_gen, "gen");
VARIANT_TAG(json_archive, currency::txin_to_script, "script");
VARIANT_TAG(json_archive, currency::txin_to_scripthash, "scripthash");
VARIANT_TAG(json_archive, currency::txin_to_key, "key");
VARIANT_TAG(json_archive, currency::txout_to_script, "script");
VARIANT_TAG(json_archive, currency::txout_to_scripthash, "scripthash");
VARIANT_TAG(json_archive, currency::txout_to_key, "key");
VARIANT_TAG(json_archive, currency::transaction, "tx");
VARIANT_TAG(json_archive, currency::block, "block");

VARIANT_TAG(debug_archive, currency::txin_gen, "gen");
VARIANT_TAG(debug_archive, currency::txin_to_script, "script");
VARIANT_TAG(debug_archive, currency::txin_to_scripthash, "scripthash");
VARIANT_TAG(debug_archive, currency::txin_to_key, "key");
VARIANT_TAG(debug_archive, currency::txout_to_script, "script");
VARIANT_TAG(debug_archive, currency::txout_to_scripthash, "scripthash");
VARIANT_TAG(debug_archive, currency::txout_to_key, "key");
VARIANT_TAG(debug_archive, currency::transaction, "tx");
VARIANT_TAG(debug_archive, currency::block, "block");
