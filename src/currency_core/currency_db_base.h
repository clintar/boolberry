// Copyright (c) 2012-2013 The Boolberry developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "include_base_utils.h"

#include "blockchain_basic_tructs.h"
#include "leveldb/db.h"
#include "leveldb/status.h"
#include "common/boost_serialization_helper.h"
#include "currency_boost_serialization.h"
#include "difficulty.h"
#include "common/difficulty_boost_serialization.h"
#include "currency_format_utils.h"

namespace currency
{
  namespace db
  {
    typedef leveldb::DB* db_handle;

    static const db_handle err_handle = nullptr;

    class basic_db
    {
      std::string m_path;
      db_handle m_pdb;
      bool m_syncdb = true;
    public:
      basic_db() :m_pdb(nullptr)
      {
      }
      ~basic_db(){ close(); }
      bool close()
      {
        if (m_pdb)
          delete m_pdb;
        m_pdb = nullptr;

        return true;
      }
      bool open(const std::string& path)
      {
        m_path = path;
        close();
        leveldb::Options options;
        options.create_if_missing = true;
        leveldb::Status status = leveldb::DB::Open(options, path, &m_pdb);
        if (!status.ok())
        {
          LOG_ERROR("Unable to open/create test database " << path << ", error: " << status.ToString());
          return err_handle;
        }
        return true;
      }
      bool empty()
      {
        leveldb::Iterator* it = m_pdb->NewIterator(leveldb::ReadOptions());
        it->SeekToFirst();
        if(it->Valid())
        {
          return 1;
        }  
        else
        {
          return 0;
        }
      }
      bool get_sync()
      {
        return m_syncdb;
      }
      void set_sync(bool sync)
      {
        m_syncdb = sync;
      }
      template<class t_pod_key>
      bool erase(const t_pod_key& k)
      {
        TRY_ENTRY();
        leveldb::WriteOptions wo;
        wo.sync = m_syncdb;
        std::string res_buff;
        leveldb::Status s = m_pdb->Delete(wo, k);
        if (!s.ok())
          return false;

        return true;
        CATCH_ENTRY_L0("get_t_object_from_db", false);
      }
        

      template<class t_pod_key, class t_object>
      bool get_t_object(const t_pod_key& k, t_object& obj)
      {
        TRY_ENTRY();
        leveldb::ReadOptions ro;
        std::string res_buff;
        leveldb::Status s = m_pdb->Get(ro, k, &res_buff);
        if (!s.ok())
          return false;

        return tools::unserialize_obj_from_buff(obj, res_buff);
        CATCH_ENTRY_L0("get_t_object_from_db", false);
      }

      bool clear()
      {
        close();
        boost::system::error_code ec;
        bool res = boost::filesystem::remove_all(m_path, ec);
        if (!res)
        {
          LOG_ERROR("Failed to remove db file " << m_path << ", why: " << ec);
          return false;
        }
        return open(m_path);
      }
      
      template<class t_pod_key, class t_object>
      bool set_t_object(const t_pod_key& k, t_object& obj)
      {
        TRY_ENTRY();
        leveldb::WriteOptions wo;
        wo.sync = m_syncdb;
        std::string obj_buff;
        tools::serialize_obj_to_buff(obj, obj_buff);

        leveldb::Status s = m_pdb->Put(wo, k, leveldb::Slice(obj_buff));
        if (!s.ok())
          return false;

        return true;
        CATCH_ENTRY_L0("set_t_object_to_db", false);
      }

      template<class t_pod_key, class t_pod_object>
      bool get_pod_object(const t_pod_key& k, t_pod_object& obj)
      {
        static_assert(std::is_pod<t_pod_object>::value, "t_pod_object must be a POD type.");

        TRY_ENTRY();
        leveldb::ReadOptions ro;
        std::string res_buff;
        leveldb::Status s = m_pdb->Get(ro, k, &res_buff);
        if (!s.ok())
          return false;

        CHECK_AND_ASSERT_MES(sizeof(t_pod_object) == res_buff.size(), false, "sizes mismatch at get_pod_object_from_db(). returned size = " 
          << res_buff.size() << "expected: " << sizeof(t_pod_object));
        
        obj = *(t_pod_object*)res_buff.data();
        return true;
        CATCH_ENTRY_L0("get_t_object_from_db", false);
      }

      template<class t_pod_key, class t_pod_object>
      bool set_pod_object(const t_pod_key& k, const t_pod_object& obj)
      {
        static_assert(std::is_pod<t_pod_object>::value, "t_pod_object must be a POD type.");

        TRY_ENTRY();
        leveldb::WriteOptions wo;
        wo.sync = m_syncdb;
        std::string obj_buff((const char*)&obj, sizeof(obj));

        leveldb::Status s = m_pdb->Put(wo, k, leveldb::Slice(obj_buff));
        if (!s.ok())
          return false;

        return true;
        CATCH_ENTRY_L0("get_t_object_from_db", false);
      }


    };


#define DB_COUNTER_KEY_NAME  "counter"
    template<class t_value>
    class vector_accessor
    {
    public: 
      basic_db bdb;
      std::shared_ptr<const t_value> local_copy;
      size_t index_of_local_copy;
      size_t count;

      vector_accessor()
        :count(0),
        index_of_local_copy(std::numeric_limits<size_t>::max())
      {
      }

      bool init(const std::string& db_path)
      {
        bool res = bdb.open(db_path);
        CHECK_AND_ASSERT_MES(res, false, "Failed to open db at path " << db_path);
        if (!bdb.get_pod_object(DB_COUNTER_KEY_NAME, count))
        {
          res = bdb.set_pod_object(DB_COUNTER_KEY_NAME, count);
          CHECK_AND_ASSERT_MES(res, false, "Failed to set counter key to db");
        }
        return true;
      }
      void push_back(const t_value& bei)
      {
        bdb.set_t_object(count++, bei);
        bdb.set_pod_object(DB_COUNTER_KEY_NAME, count);
        //update cache
        if (index_of_local_copy == count - 1)
        {
          local_copy.reset(new t_value(bei));;
        }
        LOG_PRINT_MAGENTA("[BLOCKS.PUSH_BACK], block id: " << currency::get_block_hash(bei.bl) << "[" << count - 1 << "]", LOG_LEVEL_4);

      }
      size_t size()
      {
        return  count;
      }
      size_t clear()
      {
        bdb.clear();
        count = 0;
        bdb.set_pod_object(DB_COUNTER_KEY_NAME, count);
        return true;
      }
      void pop_back()
      {
        --count;
        bdb.erase(count);
        bdb.set_pod_object(DB_COUNTER_KEY_NAME, count);
      }
      std::shared_ptr<const t_value> operator[] (size_t i)
      {
        if (i != index_of_local_copy)
        {
          block_extended_info* pbei = new block_extended_info();
          local_copy.reset(pbei);
          if (!bdb.get_t_object(i, *pbei))
          {
            LOG_ERROR("WRONG INDEX " << i << " IN DB");
            return local_copy;
          }
          index_of_local_copy = i;
        }
        LOG_PRINT_MAGENTA("[BLOCKS.[ " << i << " ]], block id: " << currency::get_block_hash(local_copy->bl), LOG_LEVEL_4);
        return local_copy;
      }
      std::shared_ptr<const t_value> back()
      {
        return this->operator [](count - 1);
      }

    };

    /************************************************************************/
    /* transactional model implementation                                   */
    /************************************************************************/
    class at_master_base
    {
      basic_db& master_db;
      std::string cid;

    public:
      size_t    count;

      at_master_base(basic_db& mdb, const std::string& container_id)
        :
        master_db(mdb),
        cid(container_id),
        count(0)
      {}
      bool get_sync()
      {
        return master_db.get_sync();
      }
      bool empty()
      {
        return master_db.empty();
      }
      template<class t_pod_key>
      std::string get_composite_key(const t_pod_key& k)
      {
        std::string key(sizeof(t_pod_key)+cid.size(), 0);
        memcpy(&key[0], &cid[0], cid.size());
        memcpy(&key[cid.size()], &k, sizeof(k));
        return key;
      }
      std::string get_composite_key(const std::string& k)
      {
        std::string key(k.size()+ cid.size(), 0);
        memcpy(&key[0], &cid[0], cid.size());
        memcpy(&key[cid.size()], &k[0], k.size());
        return key;
      }

      template<class t_key, class t_pod_object>
      bool get_pod_object(const t_key& k, t_pod_object& obj)
      {
        return master_db.get_pod_object(get_composite_key(k), obj);
      }

      template<class t_pod_key, class t_pod_object>
      bool set_pod_object(const t_pod_key& k, const t_pod_object& obj)
      {
        return master_db.set_pod_object(get_composite_key(k), obj);
      }

      template<class t_pod_key, class t_object>
      bool get_t_object(const t_pod_key& k, t_object& obj)
      {
        return master_db.get_t_object(get_composite_key(k), obj);
      }

      template<class t_pod_key, class t_object>
      bool set_t_object(const t_pod_key& k, t_object& obj)
      {
        return master_db.set_t_object(get_composite_key(k), obj);
      }
      template<class t_pod_key>
      bool erase(const t_pod_key& k)
      {
        return master_db.erase(get_composite_key(k));
      }

      bool init()
      {
        if (!get_pod_object(DB_COUNTER_KEY_NAME, count))
        {
           LOG_PRINT_MAGENTA("No count read from DB" << DB_COUNTER_KEY_NAME, LOG_LEVEL_4);
          bool res = set_pod_object(DB_COUNTER_KEY_NAME, count);
          CHECK_AND_ASSERT_MES(res, false, "Failed to set counter key to db");
        }
        return true;
      }

      void set_counter(size_t c)
      {
        LOG_PRINT_MAGENTA("count sent to DB:" << c << ", key name: " << DB_COUNTER_KEY_NAME, LOG_LEVEL_4);
        set_pod_object(DB_COUNTER_KEY_NAME, count);
      }

    };



    template<class t_value>
    class unordered_map_at_master : public at_master_base
    {
    public:
      class MyIterator : public std::iterator<std::forward_iterator_tag, int>
    {
        public:
            MyIterator(int* data, int length, bool end = false)
            {
                this->data= data;
                this->length= length;
                if(end)
                    pointer=-1;
            }
            MyIterator& operator++()
            {
                if(pointer!= length-1) {
                    pointer++;
                }
                else {
                    pointer= -1;
                }
                return *this;
            }

            bool operator==(const MyIterator& other) const { return pointer==other.pointer; }
            bool operator!=(const MyIterator& other) const { return pointer!= other.pointer; }
            int& operator*() const
            {
                if(pointer==-1)
                    return nullvalue;
                else
                    return data[pointer];
            }
        private:
            value_type* data     = nullptr;
            int length;
            int pointer          = 0;
            mutable value_type nullvalue = 0;
    };

    class MyConstIterator : public std::iterator<std::forward_iterator_tag, const int>
    {
        public:
//            MyConstIterator(int const* data, int length, bool end = false)
//            {
//                this->data= data;
//                this->length= length;
//                if(end)
//                    pointer=-1;
//            }
//            MyConstIterator& operator++()
//            {
//                if(pointer!= length-1) {
//                    pointer++;
//                }
//                else {
//                    pointer= -1;
//                }
//                return *this;
//            }
//
//            bool operator==(const MyConstIterator& other) const { return pointer==other.pointer; }
//            bool operator!=(const MyConstIterator& other) const { return pointer!= other.pointer; }
//            int const& operator*() const
//            {
//                if(pointer==-1)
//                    return nullvalue;
//                else
//                    return data[pointer];
//            }
//        private:
//            value_type* data     = nullptr;
//            int length;
//            int pointer          = 0;
//            value_type nullvalue = 0;
    };

public:
    typedef MyIterator iterator_type;
    typedef MyConstIterator const_iterator_type;
      unordered_map_at_master(basic_db& mdb, const std::string& container_id):
        at_master_base(mdb, container_id)
      {}
        
      typedef std::map<crypto::hash, std::shared_ptr<t_value> > cache_type;
      cache_type local_cache;
      
      const t_value insert(const t_value& v)
      {
        set_t_object(count++, v);
        set_counter(count);
        //update cache
        std::pair<size_t, std::shared_ptr<t_value> > vt(count, std::shared_ptr<t_value>(new t_value(v)));
        //local_cache.insert(vt);
        LOG_PRINT_MAGENTA("[VEC: PUSH_BACK], block [" << count - 1 << "]", LOG_LEVEL_4);
        return v;
      }
      
      std::shared_ptr<t_value> end()
      {
        std::shared_ptr<t_value> shv(new t_value());
        return &shv;
      }  
      
      size_t size()
      {
        return  count;
      }
      
     
      iterator_type find(crypto::hash i)
      {
        auto it = local_cache.find(i);
        if (local_cache.empty() || it == local_cache.end())
        {
          std::shared_ptr<t_value> shv(new t_value());
          if (!get_t_object(i, &shv))
          {
            LOG_ERROR("WRONG INDEX " << i << " IN DB");
            return &shv;
          }
          return &shv;
        }
        LOG_PRINT_MAGENTA("[BLOCKS.[ " << i << " ]]", LOG_LEVEL_4);
        return it;
      }
      std::shared_ptr<const t_value> operator[] (crypto::hash i)
      {
        auto it = local_cache.find(i);
        if (local_cache.empty() || it == local_cache.end())
        {
          std::shared_ptr<t_value> shv(new t_value());
          if (!get_t_object(i, *shv.get()))
          {
            LOG_ERROR("WRONG INDEX " << i << " IN DB");
            return shv;
          }
          return shv;
        }
        LOG_PRINT_MAGENTA("[BLOCKS.[ " << i << " ]]", LOG_LEVEL_4);
        return it;
      }
      
      size_t clear()
      {
        //master_db.clear();
        count = 0;
        set_counter(count);
        return true;
      }
    };    

    template<class t_value>
    class vector_at_master : public at_master_base
    {
    public:

      vector_at_master(basic_db& mdb, const std::string& container_id):
        at_master_base(mdb, container_id)
      {}

      typedef std::map<size_t, std::shared_ptr<t_value> > cache_type;
      cache_type local_cache;
      
      /* attempt to just unserialize instead of convert 
      t_value temp;
      friend class boost::serialization::access;
      
      template<class Archive>
      void save(Archive & ar, const unsigned int version) const {
        //ar & d1_ & d2_;
      }
      template<class Archive>
        void load(Archive & ar, const unsigned int version) {
          ar & temp;
          
          push_back(temp);
          delte te
        }
      BOOST_SERIALIZATION_SPLIT_MEMBER()
       */
      void push_back(const t_value& v)
      {
        set_t_object(count++, v);
        set_counter(count);
        //update cache
        std::pair<size_t, std::shared_ptr<t_value> > vt(count, std::shared_ptr<t_value>(new t_value(v)));
        //local_cache.insert(vt);
        //LOG_PRINT_MAGENTA("[VEC: PUSH_BACK], block [" << count - 1 << "]", LOG_LEVEL_4);
        LOG_PRINT_MAGENTA("[BLOCKS.PUSH_BACK], block id: " << currency::get_block_hash(v.bl) << "[" << count - 1 << "]", LOG_LEVEL_4);
      }
      size_t size()
      {
        return  count;
      }
      size_t clear()
      {
        //master_db.clear();
        count = 0;
        set_counter(count);
        return true;
      }
      
      void pop_back()
      {
        --count;
        erase(count);
        set_pod_object(DB_COUNTER_KEY_NAME, count);
      }

      std::shared_ptr<const t_value> operator[] (size_t i)
      {
        auto it = local_cache.find(i);
        if (local_cache.empty() || it == local_cache.end())
        {
          std::shared_ptr<t_value> shv(new t_value());
          if (!get_t_object(i, *shv.get()))
          {
            LOG_ERROR("WRONG INDEX " << i << " IN DB");
            return shv;
          }
          return shv;
        }
        LOG_PRINT_MAGENTA("[BLOCKS.[ " << i << " ]]", LOG_LEVEL_4);
        return it->second;
      }
      std::shared_ptr<const t_value> back()
      {
          return this->operator [](count - 1);
      }

    };
    
    
   }

}
