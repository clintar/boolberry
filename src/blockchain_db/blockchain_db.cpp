// Copyright (c) 2014, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "blockchain_db.h"
#include "currency_core/currency_format_utils.h"
#include "profile_tools.h"

using epee::string_tools::pod_to_hex;
using namespace epee;

namespace currency
{

void BlockchainDB::pop_block()
{
  block blk;
  std::vector<transaction> txs;
  pop_block(blk, txs);
}

void BlockchainDB::add_transaction(const crypto::hash& blk_hash, const transaction& tx, const crypto::hash* tx_hash_ptr)
{
  crypto::hash tx_hash;
  if (!tx_hash_ptr)
  {
    // should only need to compute hash for miner transactions
    tx_hash = get_transaction_hash(tx);
    LOG_PRINT_L3("null tx_hash_ptr - needed to compute: " << tx_hash);
  }
  else
  {
    tx_hash = *tx_hash_ptr;
  }

  for (const txin_v& tx_input : tx.vin)
  {
    if (tx_input.type() == typeid(txin_to_key))
    {
      add_spent_key(boost::get<txin_to_key>(tx_input).k_image);
    }
    else if (tx_input.type() == typeid(txin_gen))
    {
      /* nothing to do here */
    }
    else
    {
      LOG_PRINT_L1("Unsupported input type, removing key images and aborting transaction addition");
      for (const txin_v& tx_input : tx.vin)
      {
        if (tx_input.type() == typeid(txin_to_key))
        {
          remove_spent_key(boost::get<txin_to_key>(tx_input).k_image);
        }
      }
      return;
    }
  }

  uint64_t tx_id = add_transaction_data(blk_hash, tx, tx_hash);

  std::vector<uint64_t> amount_output_indices;
  // iterate tx.vout using indices instead of C++11 foreach syntax because
  // we need the index
  for (uint64_t i = 0; i < tx.vout.size(); ++i)
  {
    amount_output_indices.push_back(add_output(tx_hash, tx.vout[i], i, tx.unlock_time));
  }
  add_tx_amount_output_indices(tx_id, amount_output_indices);
}

bool BlockchainDB::push_block_scratchpad_data_db(const block& blk) 
{
  block_txn_start(false);
  // Update Scratchpad, taken from 'bool push_block_scratchpad_data(const block& b, std::vector<crypto::hash>& scratchpd)'
  std::map<uint64_t, crypto::hash> patch;
  size_t inital_sz = scratchsize();
  
  if(currency::get_block_height(blk))
  {
    crypto::hash prev = blk.prev_id;
    push_scratch(prev);
  }
  crypto::public_key tx_pub;
  bool r = parse_and_validate_tx_extra(blk.miner_tx, tx_pub);
  CHECK_AND_ASSERT_MES(r, false, "wrong miner tx in put_block_scratchpad_data: no one-time tx pubkey");
  crypto::hash  pub = *reinterpret_cast<crypto::hash*>(&tx_pub);
  push_scratch(pub);
  crypto::hash treehash = get_tx_tree_hash(blk);
  push_scratch(treehash);

  for(const auto& out: blk.miner_tx.vout)
  {
    CHECK_AND_ASSERT_MES(out.target.type() == typeid(txout_to_key), false, "wrong tx out type in coinbase!!!");
    /*
    tx outs possible to fill with nonrandom data, let's hash it with prev_tx to avoid nonrandom data in scratchpad
    */
    std::string blob;
    string_tools::apped_pod_to_strbuff(blob, blk.prev_id);
    string_tools::apped_pod_to_strbuff(blob, boost::get<txout_to_key>(out.target).key);
    crypto::hash togetherhash = crypto::cn_fast_hash(blob.data(), blob.size());
    push_scratch(togetherhash);
  }

  size_t end_entry = scratchsize();
  if(inital_sz != 0)
  {
    for(size_t i = inital_sz; i != end_entry; i++)
    {
      crypto::hash tmp = get_scratch(i);
      size_t rnd_upd_ind = reinterpret_cast<const uint64_t*>(&tmp)[0] % inital_sz;
      patch[rnd_upd_ind] = crypto::xor_pod(patch[rnd_upd_ind], get_scratch(i));
    }
  }
  for(auto& p: patch)
  {
    crypto::hash last_xor = crypto::xor_pod(get_scratch(p.first), p.second);
    update_scratch(p.first, last_xor);
  }
  return true;
}
bool BlockchainDB::update_transaction_data_local(const crypto::hash& blk_hash, const transaction& tx, const crypto::hash& tx_hash)
{
  bool retval;
  block_txn_start(false);
  retval = update_transaction_data(blk_hash, tx, tx_hash);
  block_txn_stop();
  return retval;
}

bool BlockchainDB::update_pruned_height_local(uint64_t pos)
{
  bool retval;
  block_txn_start(false);
  retval = update_pruned_height(pos);
  block_txn_stop();
  return retval;
}

uint64_t BlockchainDB::add_block( const block& blk
                                , const size_t& block_size
                                , const wide_difficulty_type& cumulative_difficulty
                                , const uint64_t& coins_generated
                                , const uint64_t& coins_donated
                                , const std::vector<transaction>& txs
                                , const uint64_t& scratch_offset
                                )
{
  block_txn_start(false);
  TIME_MEASURE_START(time1);
  crypto::hash blk_hash = get_block_hash(blk);
  TIME_MEASURE_FINISH(time1);
  time_blk_hash += time1;

  tx_extra_info ei = AUTO_VAL_INIT(ei);
  parse_and_validate_tx_extra(blk.miner_tx, ei);
  

  // Add Alias
  if(is_coinbase(blk.miner_tx) && ei.m_alias.m_alias.size())
  {
//    LOG_PRINT_L1("Adding alias " << ei.m_alias.m_alias << " to db");
    add_alias_info(ei.m_alias);
  }

  // call out to add the transactions
  time1 = epee::misc_utils::get_tick_count();
  add_transaction(blk_hash, blk.miner_tx);
  int tx_i = 0;
  crypto::hash tx_hash = null_hash;
  for (const transaction& tx : txs)
  {
    tx_hash = blk.tx_hashes[tx_i];
    add_transaction(blk_hash, tx, &tx_hash);
    ++tx_i;
  }
  TIME_MEASURE_FINISH(time1);
  time_add_transaction += time1;

  // call out to subclass implementation to add the block & metadata
  time1 = epee::misc_utils::get_tick_count();
  add_block(blk, block_size, cumulative_difficulty, coins_generated, coins_donated, blk_hash, scratch_offset);
  TIME_MEASURE_FINISH(time1);
  time_add_block1 += time1;
  
  // DB's new height based on this added block is only incremented after this
  // function returns, so height() here returns the new previous height.
  uint64_t prev_height = height();

  block_txn_stop();
  ++num_calls;

  return prev_height;
}

void BlockchainDB::pop_block(block& blk, std::vector<transaction>& txs)
{
  blk = get_top_block();

  remove_block();
  
  remove_transaction(get_transaction_hash(blk.miner_tx));
  for (const auto& h : blk.tx_hashes)
  {
    txs.push_back(get_tx(h));
    remove_transaction(h);
  }
}

bool BlockchainDB::is_open() const
{
  return m_open;
}

void BlockchainDB::remove_transaction(const crypto::hash& tx_hash)
{
  transaction tx = get_tx(tx_hash);

  for (const txin_v& tx_input : tx.vin)
  {
    if (tx_input.type() == typeid(txin_to_key))
    {
      remove_spent_key(boost::get<txin_to_key>(tx_input).k_image);
    }
  }

  // need tx as tx.vout has the tx outputs, and the output amounts are needed
  remove_transaction_data(tx_hash, tx);
}

block BlockchainDB::get_block_from_height(const uint64_t& height) const
{
  blobdata bd = get_block_blob_from_height(height);
  block b;
  if (!parse_and_validate_block_from_blob(bd, b))
    throw new DB_ERROR("Failed to parse block from blob retrieved from the db");

  return b;
}

block BlockchainDB::get_block(const crypto::hash& h) const
{
  blobdata bd = get_block_blob(h);
  block b;
  if (!parse_and_validate_block_from_blob(bd, b))
    throw new DB_ERROR("Failed to parse block from blob retrieved from the db");

  return b;
}

bool BlockchainDB::get_tx(const crypto::hash& h, currency::transaction &tx) const
{
  blobdata bd;
  if (!get_tx_blob(h, bd))
    return false;
  if (!parse_and_validate_tx_from_blob(bd, tx))
    throw new DB_ERROR("Failed to parse transaction from blob retrieved from the db");

  return true;
}

transaction BlockchainDB::get_tx(const crypto::hash& h) const
{
  transaction tx;
  if (!get_tx(h, tx))
    throw new TX_DNE(std::string("tx with hash ").append(epee::string_tools::pod_to_hex(h)).append(" not found in db").c_str());
  return tx;
}

void BlockchainDB::reset_stats()
{
  num_calls = 0;
  time_blk_hash = 0;
  time_tx_exists = 0;
  time_add_block1 = 0;
  time_add_transaction = 0;
  time_commit1 = 0;
}

void BlockchainDB::show_stats()
{
  LOG_PRINT_L1(ENDL
    << "*********************************"
    << ENDL
    << "num_calls: " << num_calls
    << ENDL
    << "time_blk_hash: " << time_blk_hash << "ms"
    << ENDL
    << "time_tx_exists: " << time_tx_exists << "ms"
    << ENDL
    << "time_add_block1: " << time_add_block1 << "ms"
    << ENDL
    << "time_add_transaction: " << time_add_transaction << "ms"
    << ENDL
    << "time_commit1: " << time_commit1 << "ms"
    << ENDL
    << "*********************************"
    << ENDL
  );
}

}  // namespace currency
