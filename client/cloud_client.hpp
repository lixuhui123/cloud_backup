#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "httplib.h"
#include <unordered_map>
#include <boost/filesystem.hpp>//迭代目录
#include <boost/algorithm/string.hpp>//split头文件


class FileUtil
{
public:
	//从文件中读取所有数据
	static bool  Read(const std::string &name, std::string *body)
	{ 
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open read file\n";
			std::cout << "open file " << name << " failed" << std::endl;
			return false;
		}
		//获取文件大小
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);
		//read(char * buf,size_t size)
		fs.read(&(*body)[0], fsize);//body是指针得先解运用才能使用[]重载
		if (fs.good() == false)
		{
			std::cout << "file" << name << "read data faild!\n";
			return false;
		}
		fs.close();
		return true;
	}
	//向文件中写入数据
	static bool Write(const std::string &name, const std::string &body)
	{
		//输出流 ofstream默认打开文件的时候会清空原有的内容，当前策略是覆盖写入 
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "写入文件\n";
			std::cout << "open file" << name << "is failed" << std::endl;
			return false;
		}
		ofs.write(&body[0], body.size());
		if (ofs.good() == false)
		{
			std::cout << "file" << name << "write failed" << std::endl;
			return false;
		}
		ofs.close();
		return true;

	}

};
class DataManage
{
public:
	DataManage(const std::string & filename):_store_file(filename)
	{}
	bool Insert(const std::string &key, const std::string &val)
	{
		//插入/更新数据
		_back_list[key] = val;
		Storage();		
		return true;
	}
	bool GetEtag(const std::string &key, std::string *val)
	{
		//通过文件名获取原有Etag信息
		auto it = _back_list.find(key);
		if (it == _back_list.end())
		{
			return false;
		}
		*val = it->second;
		return true;
	}
	bool Storage()//持久化存储
	{
		//将_back_list中的数据进行持久化存储
		//序列化 src dst\r\n
		std::stringstream tmp;
		auto it = _back_list.begin();
		for (; it != _back_list.end(); ++it)
		{
			tmp << it->first << " " << it->second << "\r\n";

		}
		FileUtil::Write(_store_file, tmp.str());//清空写入，tmp.str(),获取string流里面的string对象
		return true;
	}
	bool InitLoad()//初始化加载原有数据
	{
		//1、将这个备份文件从文件找中读取出来
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2、进行字符串的处理，按照\r\n进行分割
		std::vector<std::string> list;
		boost::split(list, body,
			boost::is_any_of("\r\n"), boost::token_compress_off);


		//3、每一行按照空格进行分割，前边是key后边是val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			Insert(key, val);
		}
		//4、将key和val存储到file_list中

		return true;
	}
private:
	std::string _store_file;//持久化存储文件名称	
	std::unordered_map<std::string, std::string> _back_list;//备份文件信息
};


class CloudClient
{
public:
	CloudClient(const std::string &filename,const std::string &store_file ,const std::string srv_ip,uint16_t srv_port)
		:_listen_dir(filename),data_manage(store_file),_srv_port(srv_port),_srv_ip(srv_ip)
	{}

	bool Start()
	{
		data_manage.InitLoad();//加载以前的备份文件名称
		while (1)
		{
			std::vector<std::string> list;
			GetBackupFileList(&list);//获取到需要备份的文件名称
			if (list.size() == 0)
				std::cout << "There is no new file to backup, please add new file\n";
			for (int i = 0; i < list.size(); ++i)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;
				std::cout << pathname << " is need to backup\n";
				std::string body;
				FileUtil::Read(pathname, &body);
				//读取文件数据作为请求正文
				httplib::Client client(_srv_ip, _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//文件上传失败了，下次再传吧
					std::cout << pathname << " upload failed\n";
					std::cout << "i will try again\n";
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);//备份成功，改变哈希表中的数据
				std::cout << pathname << " upload success\n";
			}
			Sleep(10000);//休眠10s
		}
		return true;
	}

	bool GetBackupFileList(std::vector<std::string> *list)
	{
		//进行目录监控，获取指定目录下所有的文件名称
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directories(_listen_dir);
		}
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//目录是不需要备份的
				//当前我们并不做多层级目录备份，遇到目录直接越过
				continue;
			}
			std::string pathname =begin->path().string();
		    std::string name=begin->path().filename().string();
			std::string cur_etag;
			GetEtag(pathname, &cur_etag);
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);
			if (cur_etag != old_etag)
			{
				list->push_back(name);//当前etag与原有etag不等，需要备份
			}

		}
		return true;
	}//获取需要备份的文件列表
	bool GetEtag(const std::string &pathname, std::string *etag)
	{
		//etag：文件大小-文件最后一次修改时间
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + '-' + std::to_string(mtime);

		return true;
	}//计算文件的etag信息
private:
	std::string _srv_ip;
	uint16_t _srv_port;
	std::string _listen_dir;//监控的目录名称
	DataManage data_manage ;

};