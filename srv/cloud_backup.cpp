#include"cloud_backup.hpp"
#include<thread>
void compress(char * argv[])
{

    //argv[1]源文件名称
    //argv[2]压缩包名称
    _cloud_sys::CompressUtil::Compress(argv[1],argv[2]);
    std::string file=argv[2];
    file+=".txt";
    _cloud_sys::CompressUtil::UnCompress(argv[2],file.c_str());
}
void Datamange()
{

    _cloud_sys::DataManager data_manage("./test.txt");
    /*data_manage.Insert("a.txt","a.txt");
    data_manage.Insert("b.txt","b.txt.gz");
    data_manage.Insert("c.txt","c.txt.gz");
    data_manage.Insert("d.txt","d.txt");
    data_manage.Storage();//持久化存储
*/
    data_manage.InitLoad();//所有的操作应该自load之后
    data_manage.Insert("a.txt","a.txt.gz");
    std::vector<std::string > list;
    data_manage.GetAllName(&list);
    for(auto i:list)
    {
            printf("%s\n",i.c_str());

    }
    std::cout<<"-------------------------------\n";
    list.clear();
    data_manage.NOnCompressList(&list);
    for(auto i:list)
    {
        printf("%s\n",i.c_str());
    }
    data_manage.Storage();
}

void m_non_compress()
{
    _cloud_sys::NonHotCompress ncom(GZFILE_DIR,BACKUP_DIR);
    ncom.Start();
    return ;
}

void thr_http_server()
{
    _cloud_sys::Server srv;
    srv.Start();
    return;
}
int main(int argc,char *argv[] )
{
    //文件备份路径和压缩包路径不存在创建新的路径
    if(boost::filesystem::exists(GZFILE_DIR)==false)
    {
        boost::filesystem::create_directory(GZFILE_DIR);
    }
    if(boost::filesystem::exists(BACKUP_DIR)==false)
    {
        boost::filesystem::create_directory(BACKUP_DIR);
    }

    std::thread thr_compress(m_non_compress);//启动非热点文件压缩模块
    std::thread thr_server(thr_http_server);//启动通信服务端
    thr_compress.join(); //阻塞等待
    thr_server.join();
    
    return 0;
}
