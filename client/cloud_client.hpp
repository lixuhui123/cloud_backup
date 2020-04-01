#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "httplib.h"
#include <unordered_map>
#include <boost/filesystem.hpp>//����Ŀ¼
#include <boost/algorithm/string.hpp>//splitͷ�ļ�


class FileUtil
{
public:
	//���ļ��ж�ȡ��������
	static bool  Read(const std::string &name, std::string *body)
	{ 
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open read file\n";
			std::cout << "open file " << name << " failed" << std::endl;
			return false;
		}
		//��ȡ�ļ���С
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);
		//read(char * buf,size_t size)
		fs.read(&(*body)[0], fsize);//body��ָ����Ƚ����ò���ʹ��[]����
		if (fs.good() == false)
		{
			std::cout << "file" << name << "read data faild!\n";
			return false;
		}
		fs.close();
		return true;
	}
	//���ļ���д������
	static bool Write(const std::string &name, const std::string &body)
	{
		//����� ofstreamĬ�ϴ��ļ���ʱ������ԭ�е����ݣ���ǰ�����Ǹ���д�� 
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "д���ļ�\n";
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
		//����/��������
		_back_list[key] = val;
		Storage();		
		return true;
	}
	bool GetEtag(const std::string &key, std::string *val)
	{
		//ͨ���ļ�����ȡԭ��Etag��Ϣ
		auto it = _back_list.find(key);
		if (it == _back_list.end())
		{
			return false;
		}
		*val = it->second;
		return true;
	}
	bool Storage()//�־û��洢
	{
		//��_back_list�е����ݽ��г־û��洢
		//���л� src dst\r\n
		std::stringstream tmp;
		auto it = _back_list.begin();
		for (; it != _back_list.end(); ++it)
		{
			tmp << it->first << " " << it->second << "\r\n";

		}
		FileUtil::Write(_store_file, tmp.str());//���д�룬tmp.str(),��ȡstring�������string����
		return true;
	}
	bool InitLoad()//��ʼ������ԭ������
	{
		//1������������ļ����ļ����ж�ȡ����
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2�������ַ����Ĵ�������\r\n���зָ�
		std::vector<std::string> list;
		boost::split(list, body,
			boost::is_any_of("\r\n"), boost::token_compress_off);


		//3��ÿһ�а��տո���зָǰ����key�����val
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
		//4����key��val�洢��file_list��

		return true;
	}
private:
	std::string _store_file;//�־û��洢�ļ�����	
	std::unordered_map<std::string, std::string> _back_list;//�����ļ���Ϣ
};


class CloudClient
{
public:
	CloudClient(const std::string &filename,const std::string &store_file ,const std::string srv_ip,uint16_t srv_port)
		:_listen_dir(filename),data_manage(store_file),_srv_port(srv_port),_srv_ip(srv_ip)
	{}

	bool Start()
	{
		data_manage.InitLoad();//������ǰ�ı����ļ�����
		while (1)
		{
			std::vector<std::string> list;
			GetBackupFileList(&list);//��ȡ����Ҫ���ݵ��ļ�����
			if (list.size() == 0)
				std::cout << "There is no new file to backup, please add new file\n";
			for (int i = 0; i < list.size(); ++i)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;
				std::cout << pathname << " is need to backup\n";
				std::string body;
				FileUtil::Read(pathname, &body);
				//��ȡ�ļ�������Ϊ��������
				httplib::Client client(_srv_ip, _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//�ļ��ϴ�ʧ���ˣ��´��ٴ���
					std::cout << pathname << " upload failed\n";
					std::cout << "i will try again\n";
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);//���ݳɹ����ı��ϣ���е�����
				std::cout << pathname << " upload success\n";
			}
			Sleep(10000);//����10s
		}
		return true;
	}

	bool GetBackupFileList(std::vector<std::string> *list)
	{
		//����Ŀ¼��أ���ȡָ��Ŀ¼�����е��ļ�����
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
				//Ŀ¼�ǲ���Ҫ���ݵ�
				//��ǰ���ǲ�������㼶Ŀ¼���ݣ�����Ŀ¼ֱ��Խ��
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
				list->push_back(name);//��ǰetag��ԭ��etag���ȣ���Ҫ����
			}

		}
		return true;
	}//��ȡ��Ҫ���ݵ��ļ��б�
	bool GetEtag(const std::string &pathname, std::string *etag)
	{
		//etag���ļ���С-�ļ����һ���޸�ʱ��
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + '-' + std::to_string(mtime);

		return true;
	}//�����ļ���etag��Ϣ
private:
	std::string _srv_ip;
	uint16_t _srv_port;
	std::string _listen_dir;//��ص�Ŀ¼����
	DataManage data_manage ;

};