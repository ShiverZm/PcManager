#include "stdafx.h"
#include "SoftChecker.h"
#include "tinyxml/tinyxml.h"
#include "version_util.h"
#include "file_util.h"
#include "path_util.h"
#include <shellapi.h>
#include <atlrx.h>
#include <atlpath.h>
#include <plugin/enumfile.h>
#include <libheader/libheader.h>
#pragma comment(lib,"Rpcrt4.lib")
#include <Psapi.h>
#pragma comment(lib,"Psapi.lib")
#include "pinyin.h"

#include "MsiComp.h"
#include <winstl/filesystem/path.hpp>
using namespace winstl;

inline bool IsKeyDirectory(const CStringW &path);
//////////////////////////////////////////////////////////////////////////
CSoftChecker::CSoftChecker( void ):_last_modify(0)
{
	TCHAR tempdir[MAX_PATH]={0};
	GetTempPath(MAX_PATH,tempdir);
	/*_cache_name=tempdir;
	_cache_name+=L"\\ksoft_ucache_0";*/
	
	GetModuleFileName(NULL,tempdir,MAX_PATH);
	CString strPath;
	strPath.Format(_T("%s"), tempdir);
	PathRemoveFileSpec(tempdir);
	_cache_name=tempdir;
	_cache_name+=L"\\AppData\\ksoft_ucache_4";
	m_strUpdateCache = tempdir;
	m_strUpdateCache += L"\\AppData\\ksoft_upcache";


	strPath = strPath.Right(strPath.GetLength() - strPath.ReverseFind(_T('\\')) - 1);
	strPath.MakeLower();
	if (strPath == L"ksafetray.exe")
	{
		_cache_name = L":memory:";
		m_strUpdateCache = L":memory:";
	}

	_font=tempdir;
	_font+=L"\\ksoft\\data\\front.dat";
//	init_cache();
	_InitUpdateCache();

//	::InitializeCriticalSection(&m_csUninCache);

	CPathUtil::Instance();
}

int CSoftChecker::init_cache()
{
	//sqlite3_open16(L"test.db",&_db);
	if(PathFileExists(_cache_name+L"_flag")==FALSE)
	{
		DeleteFile(_cache_name);
		sqlite3_open16(_cache_name+L"_flag",&_cache);
		sqlite3_close(_cache);
	}
	
	// 打开ksoft_ucache_4
	sqlite3_open16(_cache_name,&_cache);
	sql_run(L"PRAGMA page_size=4096;");
	sql_run(L"PRAGMA synchronous=OFF;");
	sql_run(L"PRAGMA encoding = \"UTF-16\";");
	sql_run(L"create table key2id(key,id)");
	sql_run(L"create table name2id(name,id)");
	sql_run(L"create table cachetime(pr,sub,lastwrite)");
	sql_run(L"create table unin(k,name,ico,loc,uni,pr,cname,py_,pq_,pname,stat default '已安装',si default '',lastuse si default '',unique(k))");
	sql_run(L"create table lnk(name,path,size,last,ex,unique(name))");
	sql_run(L"create table remains(type,path,loc,name,unique(path))");
	sql_run(L"create index on unin(cname)");
	sql_run(L"create index unname on unin(name)");


	return 0;
}

int CSoftChecker::_InitUpdateCache()
{
	sqlite3_open16(m_strUpdateCache,&m_pUpdateDB);
	sql_run_upd(L"PRAGMA page_size=4096;");
	sql_run_upd(L"PRAGMA synchronous=OFF;");
	sql_run_upd(L"PRAGMA encoding = \"UTF-16\";");
	sql_run_upd(L"create table update_cache(id, name, cver, nver, lastupdate default '', unique(id))");
	sql_run_upd(L"create index id_index on update(id)");	//建立 id 索引

	return 0;
}

int CSoftChecker::sql_run( CString& sql )
{
	int ret=0;
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
	return ret;
}

int CSoftChecker::sql_run_upd(CString& sql)
{
	int ret=0;
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;

	ret=sqlite3_prepare16_v2(m_pUpdateDB,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
	return ret;
}

int CSoftChecker::Load( CString lib_file/*=""*/,bool reload/*=true*/ )
{
	::SetThreadLocale( MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED) ); 

	CDataFileLoader	loader;
	BkDatLibHeader new_h;

	if( loader.GetLibDatHeader(lib_file,new_h)==FALSE || (_libfile!=L""&&_ver.QuadPart==new_h.llVersion.QuadPart) )
		return 1;

	_libfile=lib_file;
	_ver=new_h.llVersion;
	if(reload)
	{
		//;
	}

	TiXmlDocument plugins;
	BkDatLibContent cont;

	if(loader.GetLibDatContent(lib_file,cont)==FALSE)
		return 2;

	_libfile=lib_file;
	_ver=new_h.llVersion;

	plugins.Parse((char*)cont.pBuffer);

	//if(false==plugins.LoadFile(CW2A(lib_file)))
	//	return 1;

	TiXmlHandle hDoc(&plugins);
	TiXmlElement* pElem;
	TiXmlHandle hroot(NULL);

	pElem=hDoc.FirstChildElement().Element();
	hroot=TiXmlHandle(pElem);
	pElem=hroot.FirstChildElement().Element();
	for (pElem; pElem; pElem=pElem->NextSiblingElement() )
	{
		Soft* sft=new Soft;
		sft->id=CA2W(pElem->Attribute("id"));
		sft->last_update=CA2W(pElem->Attribute("lastupdate"));
		sft->ver=CA2W(pElem->Attribute("ver"));
		sft->name=CA2W(pElem->Attribute("name"));
		sft->m_strMainExe=CA2W(pElem->Attribute("main"));

		_softs[sft->id]=sft;
		Soft* pSft=sft;

		for(TiXmlElement* pElem_sign=pElem->FirstChildElement("sign"); pElem_sign; pElem_sign=pElem_sign->NextSiblingElement())
		{
			Sign* sgn=new Sign;
			sgn->soft=pSft;
			sgn->func=CA2W(pElem_sign->Attribute("type"));

			for(TiXmlElement* pElem_para=pElem_sign->FirstChildElement("param");pElem_para;pElem_para=pElem_para->NextSiblingElement())
			{
				const char* txt=pElem_para->GetText();
				sgn->arg.Add( txt? CA2W(txt): L"");
			}

			if (sgn->arg.GetCount() > 0)
			{
				pSft->signs.Add(sgn);
				_signs.Add(sgn);
			}
		}
	}

	return 0;
}

CString CSoftChecker::_get_realname( Soft* pSoft )
{
	CString strMainExe;

	for ( DWORD _dwIndex = 0; _dwIndex < pSoft->signs.GetCount(); ++_dwIndex )
	{
		Sign* _pSign = pSoft->signs[_dwIndex];
		if ( !_pSign ) continue;
		int nSignCnt = _pSign->arg.GetCount();
		for ( int i = 0; nSignCnt > i; i++ )
		{
			if( _pSign->arg[i].MakeLower().Find(TEXT(".exe")) != -1 )
			{
				strMainExe = _pSign->arg[i];
				return strMainExe;
			}
		}
	}

	return strMainExe;
}

int CSoftChecker::CheckAllInstalled( InsCheckCallBack func,void* para )
{
	CString loc;
	CString _strCurVer;
	
	int _nCount = 0;

	for ( POSITION _pos = _softs.GetStartPosition(); _pos; _softs.GetNext(_pos) )
	{
		Soft* &_pSoft = _softs.GetValueAt(_pos);
		if ( !_pSoft ) continue;

		loc = L"";
		CString strMainExe;

		for ( DWORD _dwIndex = 0; _dwIndex < _pSoft->signs.GetCount(); ++_dwIndex )
		{
			Sign* _pSign = _pSoft->signs[_dwIndex];
			if ( !_pSign ) continue;

			CString path_key=( _pSign->arg.GetCount()>0? _pSign->arg[0]:L"");
			CString file_a=( _pSign->arg.GetCount()>1? _pSign->arg[1]:L"");
			CString file_b=( _pSign->arg.GetCount()>2? _pSign->arg[2]:L"");
			CString ver_key=( _pSign->arg.GetCount()>3? _pSign->arg[3]:L"");

			if( _pSign->func == L"1" )	 //注册表路径
			{
				if ( 0 != GetFileVersionFromReg(loc, _strCurVer, path_key, file_a, file_b ) )
					continue;
			}
			else if( _pSign->func == L"2" )// 快捷方式特征
			{
				if( 0 != GetFileVersionFromLnk(loc, _strCurVer, path_key, file_a, file_b ) )
					continue;
			}
			else if ( _pSign->func == L"3" ) //获取注册表版本
			{
				if ( 0 != GetFileVersionFromRegValue(path_key, _strCurVer) )
					continue;
			}

			//
			// 过滤掉所有特殊版本，不提示升级
			//
			if(
				_strCurVer == L"0.0.0.0" ||
				_strCurVer == L"1.0.0.0" ||
				_strCurVer == L"0.0.0.1" ||
				_strCurVer == L"1.0.0.1"
				)
				_strCurVer = _pSoft->ver;

			if( func )
			{
				if( !loc.IsEmpty() )
				{
					strMainExe = loc;
					strMainExe += TEXT("#\\|");
					strMainExe += _pSoft->m_strMainExe;
				}
				else
				{
					strMainExe = _pSoft->m_strMainExe;
				}

				int _nRet = func( _pSoft->id, _pSoft->name, 
					_pSoft->ver, _strCurVer, _pSoft->last_update, strMainExe, para);

				if ( _nRet == 0 )
				{
					m_stUpdateMgr.RemoveSoft(_pSoft->id);
				}
				else if ( _nRet == 1 ) //可以升级
				{
					_pSoft->m_strCurVer = _strCurVer;
					m_stUpdateMgr.AddSoft(_pSoft);
				}

				if ( _nRet != 0 && _nRet != 1 )
					return 1;
			}
			break;
		}

	}
	
	return 0;
}


int CSoftChecker::CheckOneInstalled( CString id,InsCheckCallBack func,void* para )
{
	CString loc;
	return CheckOneInstalled(loc,id,func,para);
}


int CSoftChecker::CheckOneInstalled( CString& loc,CString id,InsCheckCallBack func,void* para ,bool once)
{
	if(_softs.Lookup(id)==NULL)
		return -2;

	CString _strCurVer;
	CString strMainExe;

	for(size_t ___i=0; ___i < _softs[id]->signs.GetCount(); ___i++)
	{
		Sign* _pSign=_softs[id]->signs[___i];
		
		CString path_key=( _pSign->arg.GetCount()>0? _pSign->arg[0]:L"");
		CString file_a=( _pSign->arg.GetCount()>1? _pSign->arg[1]:L"");
		CString file_b=( _pSign->arg.GetCount()>2? _pSign->arg[2]:L"");
		CString ver_key=( _pSign->arg.GetCount()>3? _pSign->arg[3]:L"");

		if( _pSign->func == L"1" )
		{
			if ( 0 != GetFileVersionFromReg(loc, _strCurVer, path_key, file_a, file_b ) )
				continue;
		}
		else if( _pSign->func == L"2" )// 快捷方式特征
		{
			if( 0 != GetFileVersionFromLnk(loc, _strCurVer, path_key, file_a, file_b ) )
				continue;
		}
		else if ( _pSign->func == L"3" ) //获取注册表版本
		{
			if ( 0 != GetFileVersionFromRegValue(path_key, _strCurVer) )
				continue;
		}

		//
		// 过滤掉所有特殊版本，不提示升级
		//
		if(
			_strCurVer == L"0.0.0.0" ||
			_strCurVer == L"1.0.0.0" ||
			_strCurVer == L"0.0.0.1" ||
			_strCurVer == L"1.0.0.1"
			)
			_strCurVer = _softs[id]->ver;

		if ( func )
		{
			if( !loc.IsEmpty() )
			{
				strMainExe = loc;
				strMainExe += TEXT("#\\|");
				strMainExe += _softs[id]->m_strMainExe;
			}
			else
			{
				strMainExe = _softs[id]->m_strMainExe;
			}

			int _nRet = func( _softs[id]->id, _softs[id]->name, _softs[id]->ver, _strCurVer, _softs[id]->last_update, strMainExe , para );

			if ( _nRet == 0 )
			{
				m_stUpdateMgr.RemoveSoft(_softs[id]->id);
			}
			else if ( _nRet == 1 ) //可以升级
			{
				_softs[id]->m_strCurVer = _strCurVer;
				m_stUpdateMgr.AddSoft(_softs[id]);
			}

			if ( _nRet != 0 || _nRet != 1 )
				return 1;
		}

		if(once && loc!=L"")
			return 0;

		break;
	}

	return 0;
}


int CSoftChecker::CheckAll2Uninstall( UniCheckCallBack func,void* para )
{
	int ret=0;
	UniHook hook={this,L"unin_ex",func,para};

	// 清空记录
	__temp_keys.RemoveAll();
	__temp_names.RemoveAll();

	// 查询缓存，当缓存失效时，遍历子键并记录到缓存中
	hook.cache_name = L"unin_ex";
	ret=CheckAll2UninstallExByCache(UninstallHook,&hook);
	if(ret==1)
		return 1;

	hook.cache_name = L"unin_hklm";
	ret=CheckAll2UninstallByHKByCache(HKEY_LOCAL_MACHINE, UninstallHook, &hook);
	if(ret==1)
		return 1;
	hook.cache_name = L"unin_hkcu";
	ret=CheckAll2UninstallByHKByCache(HKEY_CURRENT_USER, UninstallHook, &hook);
	if(ret==1)
		return 1;

	return ret;
}


int CSoftChecker::CheckAll2Uninstall( UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	// 若缓存有效，直接返回。若缓存无效，遍历无效子键，刷新缓存
	CheckAll2Uninstall(NULL, NULL);

	if( has_cache()==false )
		update_cache();
	__update_lastuse();

	// 创建视图, 获取卸载软件的信息
	sql_run(L"create view un_items as select *, (uninstallname like name) as rk from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");
	//sql_run(L"create view un_items as select * from unin join soft on hide!='1' and (matchtype='1' and name=uninstallname) or (matchtype='0' and (name like uninstallname or uninstallname like pname)) ");

	// 查询缓存记录，传给回调函数
	CString sql=L"Select * From un_items join ( select min(length(uninstallname)) as mrk, max(rk) as mxk, name as mname from un_items group by uni ) on mname=name where hide!='1' and (rk or (length(uninstallname)+mxk=mrk and rk=0)) group by uni";
	//CString sql = L"select * from un_items group by uni";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(true)
	{
		ret = sqlite3_step(st);
		if(ret!=100 || sqlite3_column_count(st) == 0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for (int i=0; i < ct; i++)
		{
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i) == CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp, (wchar_t*)sqlite3_column_name16(st,i), (wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re = func(mp, para);
		if( re == 1 )
			break;
	}
	ret=sqlite3_finalize(st);

	// 取出不在大全中的卸载项
	sql=L"select * from unin where name not in (select un_items.name from un_items) ";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(true)
	{
		ret = sqlite3_step(st);
		if( ret != 100 || sqlite3_column_count(st) == 0 )
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for ( int i=0;i < ct; i++)
		{
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i) == CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			// 记录软件的所有字段
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i), (wchar_t*)sqlite3_column_text16(st,i));
		}
		
		// 记录软件的信息
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);

	return 0;
}


int CSoftChecker::CheckOne2Uninstall( CString key,UniCheckCallBack func,void* para )
{
	Transaction tran(this);
	RemoveCache(key);

	UniHook hook={this,L"unin_hklm",func,para};
	int ret=0;

	ret=CheckOne2UninstallByHK(HKEY_LOCAL_MACHINE,key,UninstallHook,&hook);
	if(ret==0)
		return 0;
	
	hook.cache_name=L"unin_hkcu";
	ret=CheckOne2UninstallByHK(HKEY_CURRENT_USER,key,UninstallHook,&hook);
	if(ret==0)
		return 0;
	
	return ret;
}

int CSoftChecker::CheckOne2Uninstall( CString key ,UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	int has=0;
	
	// 删除key的缓存，重新读取uninstall\key子键, 判断是否存在key
	has = CheckOne2Uninstall(key, NULL, NULL);
	if ( func == NULL || cbfun == NULL )
		return has;
		
	// 以下刷新缓存
	if(has_cache()==false)
		update_cache();

	sql_run(L"create view un_items as select *, (uninstallname like name) as rk from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");

	//sql_run(L"create view un_items as select * from unin join soft on hide!='1' and (matchtype='1' and name=uninstallname) or (matchtype='0' and (name like uninstallname or uninstallname like pname)) ");

	//sql_run(L"create view un_items as select *, (uninstallname like name) as rk from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");
	CString sql=L"Select * From un_items join ( select min(length(uninstallname)) as mrk,max(rk) as mxk,name as mname from un_items group by uni ) on mname=name where hide!='1' and k like ? and (rk or (length(uninstallname)+mxk=mrk and rk=0)) group by uni";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,key.GetBuffer(),key.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	while(true)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for (int i=0;i<ct;i++)
		{
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i)==CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);
	
	sql=L"select * from unin where name not in (select un_items.name from un_items) and k like ? ";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,key.GetBuffer(),key.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	while(true)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for (int i=0;i<ct;i++)
		{
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i)==CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);

	return has;
}


// key: 注册表键名, loc: 本地安装路径, 
int CSoftChecker::PowerSweep( CString key,CString loc,PowerSweepCallBack func, void* para, PowerSweepFilterCallBack fnFilter)
{
	// 清空前一款软件的路径/类型列表
	__temp.RemoveAll();

	if(loc.IsEmpty() && key.GetLength() == 38)
	{
		CStringW guid = uuid2hex(key);
		CMsiComp msiComp(guid);

		const CMsiComp::DirList &dirList = msiComp.GetDirList();
		if(!dirList.IsEmpty())
		{
			loc = dirList.GetHead();
		}
	}

	if(!loc.IsEmpty() && IsKeyDirectory(loc))
		loc.Empty();


	Transaction tran(this);
	
	//目录清扫
	//MessageBox(NULL,key,loc,MB_OK);
	int fund=0;
	loc.Trim('\\');
	loc.MakeLower();

	// 系统定义的一些目录
	if( IsSysDir(loc) )
		loc=L"";

	// "x:\"
	if( loc.GetLength() < 4 )
		loc=L"";

	// 还应排除CSIDL_PROGRAM_FILES目录

	CAtlList<CString> file_dirs;
	
	// 安装目录
	if( loc != L"" && PathFileExists(loc) )
	{
		if ( !fnFilter(loc, para) )
		{
			file_dirs.AddHead(loc);

			if( __PowerSweep(L"文件目录",loc,func,para,fund) == 1 )
				return 1;
		}
	}

	// 特殊目录, key必须非空
	if ( key.GetLength() > 0 )
	{
		// x:\windows\软件名
		TCHAR	buf[MAX_PATH] = {0};
		SHGetSpecialFolderPath(NULL,buf,CSIDL_WINDOWS,FALSE);
		wcscat_s(buf,MAX_PATH,L"\\Installer\\");

		CString spfold = buf;
		spfold += key;
		if ( PathFileExists(spfold) != FALSE )
		{
			if ( !fnFilter(spfold, para) )
			{
				if(__PowerSweep(L"文件目录",spfold,func,para,fund)==1)
					return 1;

				file_dirs.AddHead(spfold);
			}
		}

		// C:\Program Files\InstallShield Installation Information
		ZeroMemory(buf,MAX_PATH);
		SHGetSpecialFolderPath(NULL,buf,CSIDL_PROGRAM_FILES,FALSE);
		wcscat_s(buf,MAX_PATH,L"\\InstallShield Installation Information\\");

		spfold = buf;
		spfold += key;
		if ( PathFileExists(spfold)!=FALSE )
		{
			if ( !fnFilter(spfold, para) )
			{
				if(__PowerSweep(L"文件目录",spfold,func,para,fund)==1)
					return 1;
				
				file_dirs.AddHead(spfold);
			}
		}
	}

	while ( file_dirs.GetHeadPosition() )
	{
		// 检查目录是否在过滤列表中
		if ( fnFilter(file_dirs.GetHead(), para) )
		{
			file_dirs.RemoveHead();
			continue;
		}

		// 枚举以上获取的目录中的文件,不含".",".."
		CEnumFile fs(file_dirs.GetHead(),L"*.*");

		// 枚举到的子目录
		for(int i=0; i < fs.GetDirCount(); i++)
			file_dirs.AddTail(fs.GetDirFullPath(i));

		// 枚举到的文件
		for(int i=0; i < fs.GetFileCount(); i++)
		{
			CString ff = fs.GetFileFullPath(i);
			ff.MakeLower();

			// ? 将枚举到的文件添加到残留列表中
			if( __PowerSweep(L"文件目录",file_dirs.GetHead(),func,para,fund) == 1
			   || __PowerSweep(L"文件目录",ff,func,para,fund) == 1)
				return 1;
		}

		// 删除已经被遍历的头节点
		file_dirs.RemoveHead();
	}

	//快捷方式: 开始菜单、快速启动栏、启动文件夹、桌面	
	CAtlList<CString> dirs;
	//TCHAR start_menu[MAX_PATH]={0};
	//TCHAR common_start_menu[MAX_PATH]={0};
	TCHAR programs[MAX_PATH]={0};
	TCHAR common_programs[MAX_PATH]={0};
	TCHAR desktop[MAX_PATH]={0};
	TCHAR quick[MAX_PATH]={0};
	TCHAR startup[MAX_PATH]={0};

	SHGetSpecialFolderPath(NULL,programs,CSIDL_PROGRAMS,FALSE);
	//SHGetSpecialFolderPath(NULL,start_menu,CSIDL_STARTMENU,FALSE);
	SHGetSpecialFolderPath(NULL,common_programs,CSIDL_COMMON_PROGRAMS,FALSE);
	//SHGetSpecialFolderPath(NULL,common_start_menu,CSIDL_COMMON_STARTMENU,FALSE);
	SHGetSpecialFolderPath(NULL,desktop,CSIDL_DESKTOP,FALSE);
	SHGetSpecialFolderPath(NULL,quick,CSIDL_APPDATA,FALSE);
	wcscat_s(quick,MAX_PATH,L"\\Microsoft\\Internet Explorer\\Quick Launch");
	SHGetSpecialFolderPath(NULL,startup,CSIDL_STARTUP,FALSE);
	dirs.AddTail(startup);
	SHGetSpecialFolderPath(NULL,startup,CSIDL_COMMON_STARTUP,FALSE);
	dirs.AddTail(startup);

	dirs.AddTail(programs);
	dirs.AddTail(common_programs);
	dirs.AddTail(desktop);
	dirs.AddTail(quick);

	static TCHAR buf[1024] = { 0 };
	while (dirs.GetHeadPosition() && loc != L"")
	{
		CEnumFile fs(dirs.GetHead(),L"*.*");

		for(int i=0;i<fs.GetDirCount();i++)
			dirs.AddTail(fs.GetDirFullPath(i));

		for(int i=0;i<fs.GetFileCount();i++)
		{
			// 文件名以lnk结尾
			CString ff=fs.GetFileFullPath(i);
			ff.MakeLower();
			if(ff.Right(3)!=L"lnk")
				continue;

			// 取lnk指向的文件的路径
			GetLnkFullPath(ff,L"",buf);

			// 若目标文件在安装目录下且文件已不存在, 或在强力清扫列表中
			CString real_loc=buf;
			real_loc.MakeLower();
			if( real_loc.Find(loc) >= 0 && (!PathFileExists(real_loc) || __temp.Lookup(real_loc) != NULL) )
			{
				if( !IsSysDir(dirs.GetHead()) )
				{
					if(__PowerSweep(L"文件目录",dirs.GetHead(),func,para,fund)==1)
						return 1;
				}

				if(__PowerSweep(L"文件目录",ff,func,para,fund)==1)
					return 1;;
			}
		}

		dirs.RemoveHead();
	}

	//注册表项
	CAtlList<CString> re;
	if(loc!=L"")
	{
		FindReg(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",loc,FIND_VALUE,re);
		FindReg(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",loc,FIND_VALUE,re);
	}

	// ? key的判断不够严格, 凡是包含key的子键都会被添加到残留列表中
	FindReg(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",key,FIND_KEY,re);
	FindReg(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",key,FIND_KEY,re);
	FindReg(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Management\\ARPCache",key,FIND_KEY,re);
	FindReg(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Management\\ARPCache",key,FIND_KEY,re);

	//特殊注册表项，有待增强
	CAtlList<CString> keylist;
	CAtlList<CString> produ_l;


	FindReg(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData","",FIND_KEY,keylist);
	while( keylist.GetHeadPosition() )
	{
		CString sub=keylist.GetHead();
		if(loc!=L"")
		{
			sub.Replace(L"HKEY_LOCAL_MACHINE\\",L"");
			sub+=L"\\Components";

			CAtlList<CString> ttt;
			FindReg(HKEY_LOCAL_MACHINE,sub,loc,FIND_VALUE|FIND_SUB_KEY,ttt);
			while(ttt.GetHeadPosition())
			{
				CString pro=ttt.GetHead().Mid(ttt.GetHead().Find(L"\\\\")+2,40);
				if(produ_l.GetHeadPosition()==NULL||produ_l.GetHead()!=pro)
					produ_l.AddTail(pro);

				re.AddTail(ttt.GetHead().Left(ttt.GetHead().Find(L"\\\\")));
				ttt.RemoveHead();
			}
		}

		if( key!=L"" )
		{
			CAtlList<CString> ttt;
			sub.Replace(L"HKEY_LOCAL_MACHINE\\",L"");
			sub+=L"\\Products";
			CString tg=uuid2hex(key);
			FindReg(HKEY_LOCAL_MACHINE,sub,tg,FIND_KEY,ttt);
			while(ttt.GetHeadPosition())
			{
				CString pro=ttt.GetHead();
				ttt.RemoveHead();
				re.AddTail(pro);
			}
		}

		keylist.RemoveHead();
	}
	
	while(produ_l.GetHeadPosition())
	{
		FindReg(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Classes\\Installer\\Products",produ_l.GetHead(),FIND_KEY,re);		
		produ_l.RemoveHead();
	}

	//右键
	FindReg(HKEY_CLASSES_ROOT,L"Folder\\Shell",loc,FIND_VALUE|FIND_SUB_KEY,keylist);
	while(keylist.GetHeadPosition()&&loc!=L"")
	{
		CAtlList<CString> ttt;
		CString cmdkey=keylist.GetHead();//.Replace(L"HKEY_CLASSES_ROOT\\",L"");
		cmdkey.Replace(L"HKEY_CLASSES_ROOT\\Folder\\Shell\\",L"");
		cmdkey=cmdkey.Left(cmdkey.Find(L"\\",1));
		if(cmdkey!=L"")
			re.AddTail(L"HKEY_CLASSES_ROOT\\Folder\\Shell\\"+cmdkey);
		keylist.RemoveHead();
	}

	//MUICACHE
	FindReg(HKEY_USERS,L"","",FIND_KEY,keylist);
	while(keylist.GetHeadPosition()&&loc!=L"")
	{
		CAtlList<CString> ttt;
		CString cmdkey=keylist.GetHead();//.Replace(L"HKEY_CLASSES_ROOT\\",L"");
		cmdkey.Replace(L"HKEY_USERS\\",L"");
		cmdkey.Trim('\\');
		cmdkey=cmdkey+"\\Software\\Microsoft\\Windows\\ShellNoRoam\\MUICache";
		
		FindReg(HKEY_USERS,cmdkey,loc,FIND_VALUE_NAME,re);
		keylist.RemoveHead();
	}

	while(re.GetHeadPosition())
	{
		// 去除FindReg添加的双斜杠
		re.GetHead().Replace(TEXT("\\\\"), TEXT("\\"));

		if(__PowerSweep(L"注册表项",re.GetHead(),func,para,fund)==1)
			return 1;

		re.RemoveHead();
	}

	CAtlList<CString> __dir_sort;

	// __dir保存了要重启删除的目录列表，在__PowerSweep删除分支中设置
	size_t max=0;
	if(__dir.GetHeadPosition())
	{
		__dir_sort.AddHead(__dir.GetHead());
		__dir.RemoveHead();

		// 按目录路径长度排序
		while(__dir.GetHeadPosition())
		{
			for (POSITION p1=__dir_sort.GetHeadPosition();p1;__dir.GetNext(p1))
			{
				if(__dir.GetHead().GetLength()<__dir_sort.GetAt(p1).GetLength())
				{
					__dir_sort.InsertBefore(p1,__dir.GetHead());
					break;
				}
			}
			__dir.RemoveHead();
		}
	}
	
	// 删除目录
	while(__dir_sort.GetHeadPosition())
	{
		if( FALSE == RemoveDirectory(__dir_sort.GetHead()) )
			MoveFileEx(__dir_sort.GetHead(),NULL,MOVEFILE_DELAY_UNTIL_REBOOT);
		
		__dir_sort.RemoveHead();
	}


	return fund==0?-1:2;
}

int CSoftChecker::Combine( CString diff_file )
{

	::SetThreadLocale( MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED) ); 


	CDataFileLoader	loader;
	BkDatLibHeader old_h;
	BkDatLibHeader new_h;

	if(_libfile==L"")
	{
		CString lib;
		PathCombine(lib.GetBuffer(1024),diff_file,L"..\\softmgrup.dat");
		Load(lib);
	}

	if(loader.GetLibDatHeader(diff_file,new_h)==FALSE
	||loader.GetLibDatHeader(_libfile,old_h)==FALSE
	||old_h.llVersion.QuadPart!=new_h.llUpdateForVer.QuadPart)
		return 1;

	TiXmlDocument plugins;

	BkDatLibContent cont;

	loader.GetLibDatContent(diff_file,cont);

	//plugins.LoadFile(CW2A(diff_file));
	plugins.Parse((char*)cont.pBuffer);


	TiXmlHandle hDoc(&plugins);
	TiXmlElement* pElem;
	TiXmlHandle hroot(NULL);
	pElem=hDoc.FirstChildElement().Element();
	hroot=TiXmlHandle(pElem);
	pElem=hroot.FirstChildElement().Element();
	for(pElem;pElem;pElem=pElem->NextSiblingElement())
	{
		Soft* sft=new Soft;
		const char* att=pElem->Attribute("id");
		sft->id=att;
		att=pElem->Attribute("fix");
		CString fix=att;

		if(fix==L"remove")
		{
			if(_softs.Lookup(sft->id))
			{
				_softs.RemoveKey(sft->id);
				delete sft;
			}
			continue;
		}
		Soft* pSft=NULL;
		if (fix==L"add")
		{
			_softs[sft->id]=sft;
			pSft=sft;
		}
		else
		{
			pSft=_softs[sft->id];
			delete sft;
		}

		att=pElem->Attribute("lastupdate");
		if(att)
			pSft->last_update=att;
		att=pElem->Attribute("ver");
		if(att)
			pSft->ver=att;
		att=pElem->Attribute("name");
		if(att)
			pSft->name=att;



		for(TiXmlElement* pElem_sign=pElem->FirstChildElement("sign");pElem_sign;pElem_sign=pElem_sign->NextSiblingElement())
		{
			Sign* sgn=new Sign;
			sgn->soft=pSft;
			att=pElem_sign->Attribute("type");
			if(att==NULL)
				continue;
			sgn->func=att;
			att=pElem_sign->Attribute("fix");
			if(att==NULL)
				continue;
			CString s_fix=att;

			for(TiXmlElement* pElem_para=pElem_sign->FirstChildElement("param");pElem_para;pElem_para=pElem_para->NextSiblingElement())
			{
				const char* txt=pElem_para->GetText();
				sgn->arg.Add( txt? CA2W(txt): L"");
			}

			if(s_fix==L"add")
			{
				pSft->signs.Add(sgn);
				_signs.Add(sgn);
			}
			else
			{
				size_t i=__Find(pSft->signs,sgn);
				if(i>=pSft->signs.GetCount())
					continue;
				pSft->signs.RemoveAt(i);
				i=__Find(_signs,sgn);
				if(i>=pSft->signs.GetCount())
					continue;
			}
		}
	}


	TiXmlDocument merg;//ws2s(lib->lib_file).c_str()

	TiXmlDeclaration * decl=new TiXmlDeclaration("1.0","gbk","");

	merg.LinkEndChild(decl);

	TiXmlElement* root=new TiXmlElement("update");

	merg.LinkEndChild(root);

	for(POSITION it=_softs.GetStartPosition();it;_softs.GetNext(it))
	{
		Soft& sft=*_softs.GetValueAt(it);
		TiXmlElement* pE_sft=new TiXmlElement("soft");
		root->LinkEndChild(pE_sft);
		pE_sft->SetAttribute("id",CW2A(sft.id));
		pE_sft->SetAttribute("ver",CW2A(sft.ver));
		pE_sft->SetAttribute("lastupdate",CW2A(sft.last_update));
		pE_sft->SetAttribute("name",CW2A(sft.name));

		for (size_t itr_s=0;itr_s!=sft.signs.GetCount();itr_s++)
		{
			Sign* sgn=sft.signs[itr_s];
			TiXmlElement* pE_sgn=new TiXmlElement("sign");
			pE_sft->LinkEndChild(pE_sgn);
			pE_sgn->SetAttribute("type",CW2A(sgn->func));
			for(size_t itr_p=0;itr_p!=sgn->arg.GetCount();itr_p++)
			{
				TiXmlElement* pE_para=new TiXmlElement("param");
				pE_sgn->LinkEndChild(pE_para);
				pE_para->LinkEndChild(new TiXmlText(CW2A(sgn->arg[itr_p])));
			}
		}
	}



	//old_h.llVersion=new_h.llVersion;
	TiXmlPrinter printer;
	printer.SetIndent( "\t" );
	merg.Accept( &printer );

	BkDatLibEncodeParam	paramx(enumLibTypePlugine,new_h.llVersion,(BYTE*)printer.CStr(),(DWORD)printer.Size(),1);
	loader.Save(_libfile,paramx);

	//merg.SaveFile(CW2A(_libfile));


	return 0;
}

CSoftChecker::~CSoftChecker( void )
{
//	sqlite3_close(_cache);
	sqlite3_close(m_pUpdateDB);
//	::DeleteCriticalSection(&m_csUninCache);

	CPathUtil::Destroy();
}

size_t CSoftChecker::GetSoftCount()
{
	return _softs.GetCount();
}

int CSoftChecker::Uninstall( CString cmd )
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	LPTSTR szCmdline=cmd.GetBuffer();
	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	if(__hasAVP()==false&&cmd.Find(L"360")<0&&cmd.Find(L"ksm")<0&&cmd.Find(L"KSM")<0&&CreateProcess( NULL,   // No module name (use command line)
		szCmdline,      // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		TRUE,          // Set handle inheritance to FALSE
		DEBUG_PROCESS|CREATE_DEFAULT_ERROR_MODE|CREATE_UNICODE_ENVIRONMENT|DETACHED_PROCESS|CREATE_NEW_PROCESS_GROUP,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi ))           // Pointer to PROCESS_INFORMATION structure
	{
		DEBUG_EVENT evt={0};

		CAtlMap<DWORD,DWORD> pids;
		//pids[GetCurrentProcessId()]=GetCurrentProcessId();
		pids[pi.dwProcessId]=pi.dwProcessId;

		HANDLE wf=pi.hProcess;
		DWORD wf_pid=pi.dwProcessId;
		DWORD last_pid=pi.dwProcessId;

		while(true)
		{
			BOOL re=TRUE;
			while(FALSE==(re=WaitForDebugEvent(&evt,INFINITE)))
			{
				Sleep(55);

			}
			switch(evt.dwDebugEventCode)
			{
			case EXIT_PROCESS_DEBUG_EVENT:
				pids.RemoveKey(evt.dwProcessId);
				DebugActiveProcessStop(evt.dwProcessId);
				break;
			case CREATE_PROCESS_DEBUG_EVENT:
				{
					CString exe;
					if(evt.u.CreateProcessInfo.lpImageName)
					{
						
						if(evt.u.CreateProcessInfo.fUnicode)
							exe=(wchar_t*)evt.u.CreateProcessInfo.lpImageName;
						else
							exe=(char*)evt.u.CreateProcessInfo.lpImageName;
					}
					if(exe==L"")
					{
						TCHAR buf[MAX_PATH]={0};
						GetProcessImageFileName(evt.u.CreateProcessInfo.hProcess,buf,MAX_PATH);
						exe=buf;
					}
					if(exe==L"")
					{
						TCHAR buf[MAX_PATH]={0};					
						HANDLE p=OpenProcess(PROCESS_ALL_ACCESS ,FALSE,evt.dwProcessId);
						GetModuleFileNameEx(p,NULL,buf,MAX_PATH);
						CloseHandle(p);
						exe=buf;
					}
					exe.MakeLower();
					if(exe.Find(L"explor")>=0||exe.Find(L"opera")>=0
						||exe.Find(L"firefox")>=0||exe.Find(L"tt")>=0
						||exe.Find(L"chrome")>=0||exe.Find(L"360se")>=0
						||exe.Find(L"theworld")>=0||exe.Find(L"maxth")>=0)
						break;
					else
					{
						pids.SetAt(GetProcessId(evt.u.CreateProcessInfo.hProcess),0);
						if(last_pid!=wf_pid)
							DebugActiveProcessStop(last_pid);
						last_pid=GetProcessId(evt.u.CreateProcessInfo.hProcess);
					}
					
				}
				break;
			case EXCEPTION_DEBUG_EVENT:

				if(evt.u.Exception.ExceptionRecord.ExceptionFlags&EXCEPTION_NONCONTINUABLE)
					ContinueDebugEvent(evt.dwProcessId,evt.dwThreadId,DBG_EXCEPTION_NOT_HANDLED);
				else if(evt.u.Exception.ExceptionRecord.ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
					ContinueDebugEvent(evt.dwProcessId,evt.dwThreadId,DBG_EXCEPTION_NOT_HANDLED);
				else if(evt.u.Exception.dwFirstChance)
					ContinueDebugEvent(evt.dwProcessId,evt.dwThreadId,DBG_EXCEPTION_HANDLED);
				else
					ContinueDebugEvent(evt.dwProcessId,evt.dwThreadId,DBG_CONTINUE);
				continue;
			default:
				;
			}
			for(int i=0;i<3&&FALSE==ContinueDebugEvent(evt.dwProcessId,evt.dwThreadId,DBG_CONTINUE);i++)
				Sleep(500);
			ZeroMemory(&evt,sizeof evt);
			if(pids.GetCount()==0)
				break;
		}

		//WaitForSingleObject( pi.hProcess, INFINITE );
		// Close process and thread handles. 
		DebugActiveProcessStop(wf_pid);
		CloseHandle( pi.hProcess );
		CloseHandle( pi.hThread );
	}
	else
	{
		build_snap();
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		LPTSTR szCmdline=cmd.GetBuffer();
		ZeroMemory( &si, sizeof(si) );
		si.cb = sizeof(si);
		ZeroMemory( &pi, sizeof(pi) );
		if(CreateProcess( NULL,   // No module name (use command line)
			szCmdline,      // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			CREATE_SUSPENDED,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi )==FALSE)
			return 2;           // Pointer to PROCESS_INFORMATION structure
		Sleep(1);

		__snap.RemoveKey(GetCurrentProcessId());
		__snap.RemoveKey(pi.dwProcessId);
		CAtlMap<DWORD,DWORD> pids;
		pids[GetCurrentProcessId()]=GetCurrentProcessId();
		pids[pi.dwProcessId]=pi.dwProcessId;

		ResumeThread(pi.hThread);

		WaitForSingleObject( pi.hProcess, INFINITE );

		while (true)
		{
			//Sleep(1000);
			PROCESSENTRY32 pe32;
			pe32.dwSize = sizeof( PROCESSENTRY32 );

			HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

			if( !Process32First( snap, &pe32 ) )
			{
				CloseHandle( snap );     // Must clean up the snapshot object!
				Sleep(550);
				continue;
			}

			bool end=true;

			do
			{
				CString exe=pe32.szExeFile;
				exe.MakeLower();
				OutputDebugString(exe);
				if(exe.Find(L"explor")>=0||exe.Find(L"opera")>=0
					||exe.Find(L"firefox")>=0||exe.Find(L"tt")>=0
					||exe.Find(L"chrome")>=0||exe.Find(L"360se")>=0
					||exe.Find(L"theworld")>=0||exe.Find(L"maxth")>=0
					||exe.Find(L"kinstool")>=0
					||pe32.th32ProcessID==GetCurrentProcessId())
				{
					__snap.SetAt(pe32.th32ProcessID,0);
					continue;
				}

				if(__snap.Lookup(pe32.th32ProcessID)||pids.Lookup(pe32.th32ParentProcessID)==NULL)
				{
					__snap.SetAt(pe32.th32ProcessID,0);
					continue;
				}
				else
				{
					end=false;
					pids[pe32.th32ProcessID]=pe32.th32ProcessID;
					break;
				}

				if(pids.Lookup(pe32.th32ParentProcessID,pe32.th32ParentProcessID))
				{
					pids[pe32.th32ProcessID]=pe32.th32ProcessID;
				}
				if(pids.Lookup(pe32.th32ProcessID,pe32.th32ProcessID))
				{
					end=false;
					break;
				}

			} while( Process32Next( snap, &pe32 ) );

			CloseHandle(snap);
			Sleep(550);

			if(end)
				break;
		}
		CloseHandle( pi.hProcess );
		CloseHandle( pi.hThread );

	}
	return 0;
}


int CSoftChecker::CheckAll2UninstallByHKByCache(HKEY parent, UniCheckCallBack func,void* para)
{
	LONG	ret = 0;
	HKEY	hKey = NULL;

	ret = RegOpenKeyEx( 
		parent,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);
	FILETIME ftLastWriteTime;      // last write time 

	DWORD  retCode; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		NULL,                // buffer for class name 
		NULL,// size of class string 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		&ftLastWriteTime);       // last write time 
	RegCloseKey(hKey);

	if( (retCode == ERROR_SUCCESS) && IsKeyCached(parent,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",ftLastWriteTime) )
	{
		return CheckAll2UnistallByCache((UniHook*)para);
	}
	else
	{
		// 清空卸载缓存ksoft_ucache_4
		// 缓存已经失效
		CString		strSql;
		strSql.Format(TEXT("delete from unin where cname='%s'"), ((UniHook*)para)->cache_name);
		sql_run(strSql);

		int re=CheckAll2UninstallByHK(parent,func,para);
		if(re!=1)
		{
			UpdateKeyCache(parent,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",ftLastWriteTime);
		}
		return re;
	}
}


int CSoftChecker::CheckAll2UninstallByHK( HKEY parent,UniCheckCallBack func,void* para )
{
	LONG	ret = 0;
	HKEY	hKey = NULL;
	
	ret = RegOpenKeyEx( 
		parent,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);

	TCHAR    achKey[MAX_VALUE_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues=0;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 

	DWORD i, retCode; 

	TCHAR  achValue[MAX_VALUE_LENGTH]; 
	DWORD cchValue = MAX_VALUE_LENGTH; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 

	if (cSubKeys)
	{

		for (i=0; i<cSubKeys; i++) 
		{ 
			cbName = MAX_VALUE_LENGTH;
			retCode = RegEnumKeyEx(hKey, i,
				achKey, 
				&cbName, 
				NULL, 
				NULL, 
				NULL, 
				&ftLastWriteTime); 

			HKEY hSubKey=NULL;//DisplayName
			
			CString uid=achKey;
			uid.MakeUpper();
			if(__temp_keys.Lookup(uid))
			{
				continue;
			}
			else
				__temp_keys.SetAt(uid,L"");

			//打开子键
			ret = RegOpenKeyEx( 
				hKey,
				achKey,
				0,
				KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
				&hSubKey 
				);

			CString disp_name,disp_icon,ins_loc,uni_cmd,disp_parent;
			CString key;

			key=achKey;

			DWORD value_type;
			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"DisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_name=achValue;

			if(disp_name==L"")
				continue;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"ReleaseType",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS&&_wcsicmp(achValue,L"Hotfix")==0)
			{
				continue;
			}



			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"UninstallString",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				uni_cmd=achValue;
			if(uni_cmd==L"")
			{
				uni_cmd=L"msiexec /x ";
				uni_cmd+=achKey;
			}

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"InstallLocation",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				ins_loc=achValue;
			//cout<<achValue<<endl;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"DisplayIcon",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_icon=achValue;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"ParentKeyName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_parent=achValue;
			if(disp_parent==L"OperatingSystem")
				continue;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"ParentDisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_parent=achValue;
			
			;

			//if(info.ParentDisplayName!=L"")
			//	AfxMessageBox(info.ParentDisplayName.c_str());

			DWORD dwValue=0;
			cchMaxValue=sizeof dwValue;
			ret=RegQueryValueEx(hSubKey,L"SystemComponent",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS&&dwValue==1)
			{
				CString id=FindUninstallId(key);
				if(id==L"")
					id=FindUninstallIdByName(disp_name);
				if(id==L"")
					continue;
			}

			dwValue=0;
			cchMaxValue=sizeof dwValue;
			ret=RegQueryValueEx(hSubKey,L"IsMinorUpgrade",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS&&dwValue==1)
				continue;

			dwValue=0;
			cchMaxValue=sizeof dwValue;
			ret=RegQueryValueEx(hSubKey,L"EstimatedSize",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);

			if(ins_loc.IsEmpty())
			{
				TCHAR buff[1024]={0};
				wcscpy_s(buff,1024,uni_cmd.GetBuffer());
				PathRemoveFileSpec(buff);
				CString l=buff;
				l.Trim('\\');
				if(IsSysDir(buff)==false)
					ins_loc=l;
			}

			if(ins_loc.IsEmpty())
			{
				TCHAR buff[1024]={0};
				wcscpy_s(buff,1024,disp_icon.GetBuffer());
				PathRemoveFileSpec(buff);
				CString l=buff;
				l.Trim('\\');
				if(IsSysDir(buff)==false)
					ins_loc=l;
			}

			if(ins_loc==L"")
				ins_loc=__FindInstLoc(key);

			ins_loc.Trim('\\');

			if(PathIsDirectory(ins_loc)==FALSE&&ins_loc!=L""&&(ins_loc.Find(L"-")>=0||ins_loc.Find(L"\"")>=0))
			{
				int ct=0;
				LPWSTR* szArglist=CommandLineToArgvW(ins_loc.GetBuffer(),&ct);
				if(ct>0)
					ins_loc=szArglist[0];
				LocalFree(szArglist);
				if(PathIsDirectory(ins_loc)==FALSE)
				{
					ins_loc=ins_loc.Left(ins_loc.ReverseFind('\\'));
				}
				if(PathFileExists(ins_loc)==FALSE)
					ins_loc=L"";
				ins_loc.Trim('\\');
			}

			if(func==NULL)
				continue;
			

			if(__temp_names.Lookup(uni_cmd))
			{
				continue;
			}
			else
				__temp_names.SetAt(uni_cmd,L"");
			//DWORD dwMS,dwLS;

			// 保存到缓存中, UninstallHook
			if( func(key,disp_name,disp_icon,ins_loc,uni_cmd,disp_parent,para) == 1 )
				return 1;
			
			if(dwValue>0)
			{
				__update_size(ins_loc,dwValue*1024);
			}

			//UninstallString
		}
	}


	return 0;
}

int CSoftChecker::CheckOne2UninstallByHK( HKEY parent,CString key ,UniCheckCallBack func,void* para )
{
	LONG	ret = 0;
	HKEY	hKey = NULL;

	ret = RegOpenKeyEx( 
		parent,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);

	TCHAR    achKey[MAX_VALUE_LENGTH];   // buffer for subkey name                 // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues=0;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 
	DWORD retCode; 

	TCHAR  achValue[MAX_VALUE_LENGTH]; 
	DWORD cchValue = MAX_VALUE_LENGTH; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 


	if (cSubKeys)
	{
		HKEY hSubKey=NULL;
		ret = RegOpenKeyEx( 
			hKey,
			key.GetBuffer(),
			0,
			KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
			&hSubKey 
			);

		if(ret!=ERROR_SUCCESS)
			return -1;



		CString disp_name,disp_icon,ins_loc,uni_cmd,disp_parent;

		//key=achKey;

		DWORD value_type;
		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"DisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			disp_name=achValue;

		if(disp_name==L"")
			return 1;


		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"UninstallString",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			uni_cmd=achValue;
		if(uni_cmd==L"")
		{
			uni_cmd=L"msiexec /x ";
			uni_cmd+=achKey;
		}

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"InstallLocation",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			ins_loc=achValue;
		//cout<<achValue<<endl;

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"DisplayIcon",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			disp_icon=achValue;

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"ParentDisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			disp_parent=achValue;

		if(disp_parent==L"OperatingSystem")
			return 1;

		//if(info.ParentDisplayName!=L"")
		//	AfxMessageBox(info.ParentDisplayName.c_str());

		DWORD dwValue=0;
		//cchMaxValue=sizeof dwValue;
		//ret=RegQueryValueEx(hSubKey,L"SystemComponent",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);
		//if(ret==ERROR_SUCCESS&&dwValue==1)
		//	return 1;

		dwValue=0;
		cchMaxValue=sizeof dwValue;
		ret=RegQueryValueEx(hSubKey,L"IsMinorUpgrade",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS&&dwValue==1)
			return 1;			

		dwValue=0;
		cchMaxValue=sizeof dwValue;
		ret=RegQueryValueEx(hSubKey,L"EstimatedSize",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);


		if(ins_loc==L"")
			ins_loc=__FindInstLoc(key);
		ins_loc.Trim('\\');
		if(PathIsDirectory(ins_loc)==FALSE&&ins_loc!=L""&&(ins_loc.Find(L"-")>=0||ins_loc.Find(L"\"")>=0))
		{
			int ct=0;
			LPWSTR* szArglist=CommandLineToArgvW(ins_loc.GetBuffer(),&ct);
			if(ct>0)
				ins_loc=szArglist[0];
			LocalFree(szArglist);
			if(PathIsDirectory(ins_loc)==FALSE)
			{
				ins_loc=ins_loc.Left(ins_loc.ReverseFind('\\'));
			}
			if(PathFileExists(ins_loc)==FALSE)
				ins_loc=L"";
			ins_loc.Trim('\\');
		}

		if(ins_loc.IsEmpty()&&uni_cmd.GetLength()<4000&&uni_cmd.GetLength()>0)
		{
			TCHAR buff[4096]={0};
			ATL::CPath pathUniCmd;
			int nStart = uni_cmd.Find('\"');
			if( nStart != -1 )
			{
				int nEnd = uni_cmd.Find('\"', nStart + 1 );
				if( nEnd != -1 )
				{
					pathUniCmd.m_strPath = uni_cmd.Mid( nStart+1 );
				}
				else
				{
					pathUniCmd.m_strPath = uni_cmd.Mid( nStart+1, nEnd - nStart);
				}
			}
			else
			{
				pathUniCmd.m_strPath = uni_cmd;
			}
			
			if( !pathUniCmd.IsDirectory() )
				pathUniCmd.RemoveFileSpec();

			if( IsSysDir( pathUniCmd.m_strPath ) == false )
				ins_loc = pathUniCmd.m_strPath;
		}

		if(ins_loc.IsEmpty()&&disp_icon.GetLength()<4000&&disp_icon.GetLength()>0)
		{
			TCHAR buff[4096]={0};
			wcscpy_s(buff,4096,disp_icon.GetBuffer());
			PathRemoveFileSpec(buff);
			CString l=buff;
			l.Trim('\\');
			if(IsSysDir(buff)==false)
				ins_loc=l;
		}


		if(func==NULL)
			return 1;

		//DWORD dwMS,dwLS;

		if(func(key,disp_name,disp_icon,ins_loc,uni_cmd,disp_parent,para)==1)
			return 1;
		if(dwValue)
			__update_size(ins_loc,dwValue*1024);

		//UninstallString
	}
	else
		return -1;


	return 0;
}

int CSoftChecker::FindReg( HKEY parent,CString key,CString value,DWORD flag ,CAtlList<CString>& result)
{
	value.MakeLower();
	HKEY hKey;

	if( RegOpenKeyEx( parent,
		key,
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ,
		&hKey) == ERROR_SUCCESS
		)
	{
		TCHAR    achKey[MAX_VALUE_LENGTH];   // buffer for subkey name
		DWORD    cbName;                   // size of name string 
		TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
		DWORD    cchClassName = MAX_PATH;  // size of class string 
		DWORD    cSubKeys=0;               // number of subkeys 
		DWORD    cbMaxSubKey;              // longest subkey size 
		DWORD    cchMaxClass;              // longest class string 
		DWORD    cValues;              // number of values for key 
		DWORD    cchMaxValue;          // longest value name 
		DWORD    cbMaxValueData;       // longest value data 
		DWORD    cbSecurityDescriptor; // size of security descriptor 
		FILETIME ftLastWriteTime;      // last write time 

		DWORD i, retCode; 

		TCHAR  achValue[MAX_VALUE_LENGTH]; 
		DWORD cchValue = MAX_VALUE_LENGTH; 

		// Get the class name and the value count. 
		retCode = RegQueryInfoKey(
			hKey,                    // key handle 
			achClass,                // buffer for class name 
			&cchClassName,           // size of class string 
			NULL,                    // reserved 
			&cSubKeys,               // number of subkeys 
			&cbMaxSubKey,            // longest subkey size 
			&cchMaxClass,            // longest class string 
			&cValues,                // number of values for this key 
			&cchMaxValue,            // longest value name 
			&cbMaxValueData,         // longest value data 
			&cbSecurityDescriptor,   // security descriptor 
			&ftLastWriteTime);       // last write time 

		// Enumerate the subkeys, until RegEnumKeyEx fails.

		if (cSubKeys)
		{
			for (i=0; i<cSubKeys; i++) 
			{ 
				cbName = MAX_VALUE_LENGTH;
				retCode = RegEnumKeyEx(hKey, i,
					achKey, 
					&cbName, 
					NULL, 
					NULL, 
					NULL, 
					&ftLastWriteTime); 
				if (retCode == ERROR_SUCCESS) 
				{
					CString subkey=achKey;
					subkey.MakeLower();
					
					// ?
					if(	((flag&FIND_KEY)&&subkey.Find(value)>=0) ||((flag&(FIND_KEY|FIND_EQU))&&subkey==value) )
						result.AddTail(PreKey2wsLong(parent)+L"\\"+key+L"\\"+subkey);

					if( flag & FIND_SUB_KEY )
						FindReg(parent,key+L"\\"+achKey,value,flag,result);
				}
			}
		} 

		// Enumerate the key values. 

		if (cValues) 
		{
			for (i=0, retCode=ERROR_SUCCESS; i<cValues; i++) 
			{ 
				cchValue = MAX_VALUE_LENGTH; 
				achValue[0] = '\0'; 
				DWORD type;
				TCHAR vvv[MAX_VALUE_LENGTH]={0};
				DWORD v_len=MAX_VALUE_LENGTH;
				retCode = RegEnumValue(hKey, i, 
					achValue, 
					&cchValue, 
					NULL, 
					&type,
					(LPBYTE)vvv,
					&v_len);

				if (retCode == ERROR_SUCCESS ) 
				{ 
					CString subkey=achValue;
					subkey.MakeLower();

					if(	( (flag & FIND_VALUE_NAME) && subkey.Find(value) >= 0 )
						|| ((flag & (FIND_VALUE_NAME | FIND_EQU)) && subkey == value )
						)
						result.AddTail(PreKey2wsLong(parent)+L"\\"+key+L"\\\\"+subkey);
					
					if((flag & FIND_VALUE) && type == REG_SZ )
					{
						CString s_vvv = vvv;
						s_vvv.MakeLower();
						
						if( ((flag & FIND_EQU) && value==vvv ) )
						   result.AddTail(PreKey2wsLong(parent)+L"\\"+key+L"\\\\"+subkey+"");

						if ( s_vvv.Find(value) >=0 && __temp.Lookup(s_vvv) != NULL )
						{
							result.AddTail(PreKey2wsLong(parent)+L"\\"+key+L"\\\\"+subkey+"");
						}
					}
				} 
			}
		}
	}
	return 0;
}

bool CSoftChecker::IsSysDir( CString dir )
{
	TCHAR programs[MAX_PATH]={0};
	SHGetSpecialFolderPath(NULL,programs,CSIDL_PROGRAM_FILES,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_WINDOWS,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_PROGRAMS,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_SYSTEM,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_DRIVES,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_COMMON_PROGRAMS,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_APPDATA,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	wcscat_s(programs,MAX_PATH,L"\\Microsoft\\Internet Explorer\\Quick Launch");
	CString desk=programs;
	desk.MakeLower();
	dir.MakeLower();
	if(_wcsicmp(programs,L"")!=0&&(_wcsicmp(programs,dir)==0||(PathIsDirectory(dir)&&dir.Find(desk)==0)))
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_STARTUP,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_COMMON_STARTUP,FALSE);
	if(_wcsicmp(programs,L"")!=0&&_wcsicmp(programs,dir)==0)
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_DESKTOP,FALSE);
	desk=programs;
	desk.MakeLower();
	dir.MakeLower();
	if(_wcsicmp(programs,L"")!=0&&(_wcsicmp(programs,dir)==0||(PathIsDirectory(dir)&&dir.Find(desk)==0)))
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_DESKTOPDIRECTORY,FALSE);
	desk=programs;
	desk.MakeLower();
	if(_wcsicmp(programs,L"")!=0&&(_wcsicmp(programs,dir)==0||(PathIsDirectory(dir)&&dir.Find(desk)==0)))
		return true;
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_COMMON_DESKTOPDIRECTORY,FALSE);
	desk=programs;
	desk.MakeLower();
	if(_wcsicmp(programs,L"")!=0&&(_wcsicmp(programs,dir)==0||(PathIsDirectory(dir)&&dir.Find(desk)==0)))
		return true;


	return false;
}


// 会写入到__temp中
int CSoftChecker::__PowerSweep( CString type,CString path,PowerSweepCallBack func,void* para,int& fund,CString ins_loc/*=L""*/ )
{
	if(PathIsDirectory(path)!=FALSE)
	{
		type=L"目录";
		type.Replace(L"\\\\",L"\\");
	}
	else if(PathFileExists(path)!=FALSE||type==L"文件目录")
	{
		type=L"文件";
		type.Replace(L"\\\\",L"\\");
	}

	// 保存到临时路径/类型列表中
	if(__temp.Lookup(path)!=NULL)
		return 0;
	else
		__temp[path]=type;

	fund=1;
	int act=func(type,path,para);
	switch(act)
	{
	case 3:		// 彻底删除文件
	case 2:		// 删除到回收站
		{
			__remove_remains(type,path);
			if(type==L"文件"||type==L"目录")
			{
				SHFILEOPSTRUCT fp={0};
				
				path.AppendChar(0);
				
				fp.pFrom=path.GetBuffer();
				fp.wFunc=FO_DELETE;
				fp.hwnd=GetDesktopWindow();

				if ( act == 2 )
				{
					// 删除到回收站(整个目录删除到回收站中方便用户还原)
					fp.fFlags=FOF_NO_UI|FOF_NOERRORUI|FOF_SILENT|FOF_NOCONFIRMATION | FOF_ALLOWUNDO;
				}
				else
				{
					// 彻底删除FOF_NO_UI|FOF_NOERRORUI|FOF_SILENT|
					fp.fFlags=FOF_NO_UI|FOF_NOERRORUI|FOF_SILENT|FOF_NOCONFIRMATION;
				}
				
				int err=SHFileOperation(&fp);
				err;

				if(PathFileExists(path)!=FALSE)
				{
					if(PathIsDirectory(path)!=FALSE)
					{
						// 如果用SHFileOperation没有成功删除， 则目录放到最后一起删除
						if(RemoveDirectory(path)==FALSE)
							__dir.AddHead(path);
					}
					else
					{
						if ( act == 2 )		// 需要删除到回收站中
						{
							// 重启删除之前复制一份放到回收站中， 使重启删除的文件也能恢复
							CString	strNewName;
							strNewName = path + _T( ".old" ); 
							MoveFile( path, strNewName );
							CopyFile( strNewName, path, FALSE );

							// 原名的文件放到回收站
							SHFileOperation(&fp);		
					
							MoveFileEx(strNewName,NULL,MOVEFILE_DELAY_UNTIL_REBOOT);	
						}
						else
						{
							// 不能删除的文件重启删除
							MoveFileEx(path,NULL,MOVEFILE_DELAY_UNTIL_REBOOT);
						}
					}
				 }
			}
			else if(type=="注册表项")
			{
				CString parent=path.Left(path.Find(L"\\"));
				HKEY hParent;
				if(parent==L"HKEY_LOCAL_MACHINE")
					hParent=HKEY_LOCAL_MACHINE;
				if(parent==L"HKEY_CURRENT_USER")
					hParent=HKEY_CURRENT_USER;
				if(parent=="HKEY_CLASSES_ROOT")
					hParent=HKEY_CLASSES_ROOT;
				if(parent=="HKEY_USERS")
					hParent=HKEY_USERS;
				path.Replace(parent+L"\\",L"");
				CString key=path;
				if(path.Find(L"\\\\")>0)
				{
					key=key.Left(path.Find(L"\\\\"));
					path.Replace(key+L"\\\\",L"");
					CString value=path;
					SHDeleteValue(hParent,key,value);
				}
				else
					SHDeleteKey(hParent,key);

			}
		}
		break;
	case 1:
		return 1;
	case 0:
	default:
		break;
	}
	return 0;
}

int CSoftChecker::Install( CString cmd )
{
	CString ext=cmd.Right(3);
	ext.MakeLower();
	if(ext!="exe")
	{
		ShellExecute(NULL,NULL,cmd,NULL,NULL,SW_NORMAL);
		return 0;
	}
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	LPTSTR szCmdline=cmd.GetBuffer();
	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );

	TCHAR dir[MAX_PATH]={0};
	wcscpy_s(dir,MAX_PATH,cmd.GetBuffer());
	PathRemoveFileSpec(dir);

	CreateProcess( NULL,   // No module name (use command line)
		szCmdline,      // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_NEW_CONSOLE,              // No creation flags
		NULL,           // Use parent's environment block
		dir,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi );           // Pointer to PROCESS_INFORMATION structure

	WaitForSingleObject( pi.hProcess, INFINITE );
	CloseHandle(pi.hProcess);
	return 0;
}

size_t CSoftChecker::__Find(CAtlArray<Sign*>&ar,Sign* sgn )
{
	for(size_t i=0;i<ar.GetCount();i++)
	{
		if(ar[i]->func==sgn->func&&ar[i]->arg.GetCount()==sgn->arg.GetCount())
		{
			size_t i_s=0;
			for(;i_s<ar[i]->arg.GetCount();i_s++)
			{
				if(ar[i]->arg[i_s]!=sgn->arg[i_s])
					break;
			}
			if(i_s==sgn->arg.GetCount())
			{
				for(;i_s < ar[i]->arg.GetCount()&&ar[i]->arg[i_s]!=L"";i_s++)
					;
			}
			if(i_s < ar[i]->arg.GetCount() )
				continue;
			return i;

		}
	}
	return ar.GetCount();
}


CString CSoftChecker::__FindInstLoc( CString key )
{
	for(size_t i=0;i<_signs.GetCount();i++)
	{
		if(_signs[i]->func==L"2")
			continue;
		if(_signs[i]->arg[0].Find(key)>0)
		{
			CString loc;
			if(CheckOneInstalled(loc,_signs[i]->soft->id,NULL,NULL,true)==0)
				return loc;
		}
	}
	return L"";
}

const wchar_t* CSoftChecker::FindUninstallIdByName( CString name )
{
	if(CAtlRECharTraits::Isdigit(name[0])&&name.GetLength()<4)
		return L"";

	for(POSITION i=_softs.GetStartPosition();i;)
	{
		Soft* sft=_softs.GetNextValue(i);
		if(sft->name.Find(name)==-1)
			continue;
		else
			return sft->id;
	}
	return L"";
}

const wchar_t* CSoftChecker::FindUninstallId( CString key )
{
	key.MakeLower();
	if(__key2id.Lookup(key))
		return __key2id[key];
	CString ret=L"";
	int lastlen=3000;
	for(size_t i=0;i<_signs.GetCount();i++)
	{
		if(_signs[i]->func==L"2")
			continue;
		CString sim=_signs[i]->arg[0];
		sim=sim.Right(sim.GetLength()-1-sim.Find(L"\\"));
		if(sim.Find(L"\\\\")>0)
		{
			sim=sim.Left(sim.Find(L"\\\\"));
		}
		sim.MakeLower();
		if(__key2id.Lookup(sim)==NULL)
			__key2id.SetAt(sim,_signs[i]->soft->id);
		if(sim==key)
			return _signs[i]->soft->id.GetBuffer();
		/*int pos=_signs[i]->arg[0].Find(key);
		if(pos>=0&&(_signs[i]->arg[0].GetLength()<lastlen))*/
		sim=_signs[i]->arg[0];
		sim.MakeLower();
		if(sim.Find(key)>=0&&sim.GetLength()<lastlen)
		{
			ret=_signs[i]->soft->id.GetBuffer();
			lastlen=sim.GetLength();
		}
	}
	return ret;
}

int CSoftChecker::CheckAll2UninstallByHKEx( HKEY parent,UniCheckCallBack func,void* para )
{
	LONG	ret = 0;
	HKEY	hKey = NULL;
	ret = RegOpenKeyEx( 
		parent,
		L"Products",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);

	TCHAR    achKey[MAX_VALUE_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues=0;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 

	DWORD i, retCode; 

	TCHAR  achValue[MAX_VALUE_LENGTH]; 
	DWORD cchValue = MAX_VALUE_LENGTH; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 


	if (cSubKeys)
	{

		for (i=0; i<cSubKeys; i++) 
		{ 
			cbName = MAX_VALUE_LENGTH;
			retCode = RegEnumKeyEx(hKey, i,
				achKey, 
				&cbName, 
				NULL, 
				NULL, 
				NULL, 
				&ftLastWriteTime); 

			HKEY hSubKey=NULL;//DisplayName


			//打开子键
			CString moreKey=achKey;

			
			CString uid=hex2uuid(achKey);
			uid.MakeUpper();
			/*UuidFromString((RPC_WSTR)achKey,&uid);*/

			// 查询uid是否存在
			if(__temp_keys.Lookup(uid))
			{
				continue;
			}
			else
				__temp_keys.SetAt(uid,L"");

			moreKey+=L"\\InstallProperties";
			
			
			ret = RegOpenKeyEx( 
				hKey,
				moreKey,
				0,
				KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
				&hSubKey 
				);



			CString disp_name,disp_icon,ins_loc,uni_cmd,disp_parent;
			CString key;

			key=achKey;

			DWORD value_type;
			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"DisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_name=achValue;

			if(disp_name==L"")
				continue;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"ReleaseType",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS&&_wcsicmp(achValue,L"Hotfix")==0)
			{
				continue;
			}


			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"UninstallString",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				uni_cmd=achValue;
			if(uni_cmd==L"")
			{
				uni_cmd=L"msiexec /x ";
				uni_cmd+=achKey;
			}

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"InstallLocation",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				ins_loc=achValue;
			//cout<<achValue<<endl;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"DisplayIcon",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_icon=achValue;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"ParentKeyName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_parent=achValue;
			if(disp_parent==L"OperatingSystem")
				continue;

			ZeroMemory(achValue,MAX_VALUE_LENGTH);
			cchMaxValue=MAX_VALUE_LENGTH;
			ret=RegQueryValueEx(hSubKey,L"ParentDisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS)
				disp_parent=achValue;

			;

			//if(info.ParentDisplayName!=L"")
			//	AfxMessageBox(info.ParentDisplayName.c_str());

			DWORD dwValue=0;
			cchMaxValue=sizeof dwValue;
			ret=RegQueryValueEx(hSubKey,L"SystemComponent",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS&&dwValue==1)
			{
				CString id=FindUninstallId(uid);
				/*if(id==L"")
					id=FindUninstallIdByName(disp_name);*/
				if(id==L"")
					continue;
			}

			dwValue=0;
			cchMaxValue=sizeof dwValue;
			ret=RegQueryValueEx(hSubKey,L"IsMinorUpgrade",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);
			if(ret==ERROR_SUCCESS&&dwValue==1)
				continue;

			dwValue=0;
			cchMaxValue=sizeof dwValue;
			ret=RegQueryValueEx(hSubKey,L"EstimatedSize",NULL,&value_type,(LPBYTE)&dwValue,&cchMaxValue);

			//if(disp_name.Find(L"lash")>=0)
			//	__asm int 3;

			if(ins_loc==L"")
				ins_loc=__FindInstLoc(key);

			ins_loc.Trim('\\');
			if(PathIsDirectory(ins_loc)==FALSE&&ins_loc!=L""&&(ins_loc.Find(L"-")>=0||ins_loc.Find(L"\"")>=0))
			{
				int ct=0;
				LPWSTR* szArglist=CommandLineToArgvW(ins_loc.GetBuffer(),&ct);
				if(ct>0)
					ins_loc=szArglist[0];
				LocalFree(szArglist);
				if(PathIsDirectory(ins_loc)==FALSE)
				{
					ins_loc=ins_loc.Left(ins_loc.ReverseFind('\\'));
				}
				if(PathFileExists(ins_loc)==FALSE)
					ins_loc=L"";
				ins_loc.Trim('\\');
			}

			ins_loc.Trim('\\');

			if(ins_loc.IsEmpty())
			{
				TCHAR buff[1024]={0};
				wcscpy_s(buff,1024,uni_cmd.GetBuffer());
				PathRemoveFileSpec(buff);
				CString l=buff;
				l.Trim('\\');
				if(IsSysDir(buff)==false)
					ins_loc=l;
			}

			if(ins_loc.IsEmpty())
			{
				TCHAR buff[1024]={0};
				wcscpy_s(buff,1024,disp_icon.GetBuffer());
				PathRemoveFileSpec(buff);
				CString l=buff;
				l.Trim('\\');
				if(IsSysDir(buff)==false)
					ins_loc=l;
			}

			if(func==NULL)
				continue;


			//DWORD dwMS,dwLS;
			if(__temp_names.Lookup(uni_cmd))
			{
				continue;
			}
			else
				__temp_names.SetAt(uni_cmd,L"");

			if(func(uid,disp_name,disp_icon,ins_loc,uni_cmd,disp_parent,para)==1)
				return 1;

			if(dwValue)
				__update_size(ins_loc,dwValue*1024);

			//UninstallString
		}
	}


	return 0;
}

int CSoftChecker::CheckAll2UninstallExByCache( UniCheckCallBack func,void* para )
{
	LONG	ret = 0;
	HKEY	hKey = NULL;

	ret = RegOpenKeyEx( 
		HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);
	FILETIME ftLastWriteTime;      // last write time 

	DWORD  retCode; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		NULL,                // buffer for class name 
		NULL,// size of class string 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		NULL,                    // reserved 
		&ftLastWriteTime);       // last write time 
	RegCloseKey(hKey);

	// 若子键已经被缓存且有效，使用缓存中的记录
	if( (retCode == ERROR_SUCCESS) && IsKeyCached(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData",ftLastWriteTime) )
	{
		return CheckAll2UnistallByCache((UniHook*)para);
	}
	else
	{
		// 缓存已经失效
		CString		strSql;
		strSql.Format(TEXT("delete from unin where cname='%s'"), ((UniHook*)para)->cache_name);
		sql_run(strSql);

		// 遍历子键并将结果缓存起来
		int re=CheckAll2UninstallEx(func, para);
		if( re != 1 )
		{
			// 更新子键的缓存记录的时间
			UpdateKeyCache(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData",ftLastWriteTime);
		}
		
		return re;
	}
}





int CSoftChecker::CheckAll2UninstallEx( UniCheckCallBack func,void* para )
{
	LONG	ret = 0;
	HKEY	hKey = NULL;
	
	ret = RegOpenKeyEx( 
		HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);

	TCHAR    achKey[MAX_VALUE_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues=0;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 

	DWORD i, retCode; 

	//TCHAR  achValue[MAX_VALUE_LENGTH]; 
	DWORD cchValue = MAX_VALUE_LENGTH; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 


	if (cSubKeys)
	{

		for (i=0; i<cSubKeys; i++) 
		{
			cbName = MAX_VALUE_LENGTH;
			retCode = RegEnumKeyEx(hKey, i,
				achKey, 
				&cbName, 
				NULL, 
				NULL, 
				NULL, 
				&ftLastWriteTime); 

			HKEY hSubKey=NULL;//DisplayName


			//打开子键

			ret = RegOpenKeyEx( 
				hKey,
				achKey,
				0,
				KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
				&hSubKey 
				);
			
			int re=CheckAll2UninstallByHKEx(hSubKey,func,para);
			RegCloseKey(hSubKey);
			
			if(re==1)
			{
				RegCloseKey(hKey);
				return 1;
			}
		}
	}
	RegCloseKey(hKey);
	return 0;
}


int CSoftChecker::CheckOne2UninstallByHKEx( HKEY parent,CString key2c,UniCheckCallBack func,void* para )
{
	LONG	ret = 0;
	HKEY	hKey = NULL;
	ret = RegOpenKeyEx( 
		parent,
		L"Products",
		0,
		KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
		&hKey 
		);

	TCHAR    achKey[MAX_VALUE_LENGTH];   // buffer for subkey name
	//DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues=0;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 

	DWORD retCode; 

	TCHAR  achValue[MAX_VALUE_LENGTH]; 
	DWORD cchValue = MAX_VALUE_LENGTH; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 


	if (cSubKeys)
	{

		HKEY hSubKey=NULL;//DisplayName


		//打开子键
		CString moreKey=key2c;


		CString uid=hex2uuid(achKey);
		uid.MakeUpper();
		/*UuidFromString((RPC_WSTR)achKey,&uid);*/

		moreKey+=L"\\InstallProperties";


		ret = RegOpenKeyEx( 
			hKey,
			moreKey,
			0,
			KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_READ, 
			&hSubKey 
			);

		if(ret!=ERROR_SUCCESS)
			return -1;



		CString disp_name,disp_icon,ins_loc,uni_cmd,disp_parent;
		CString key;

		key=achKey;

		DWORD value_type;
		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"DisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			disp_name=achValue;

		if(disp_name==L"")
			return 1;


		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"UninstallString",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			uni_cmd=achValue;
		if(uni_cmd==L"")
		{
			uni_cmd=L"msiexec /x ";
			uni_cmd+=achKey;
		}

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"InstallLocation",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			ins_loc=achValue;
		//cout<<achValue<<endl;

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"DisplayIcon",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			disp_icon=achValue;

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"ParentDisplayName",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS)
			disp_parent=achValue;

		ZeroMemory(achValue,MAX_VALUE_LENGTH);
		cchMaxValue=MAX_VALUE_LENGTH;
		ret=RegQueryValueEx(hSubKey,L"ReleaseType",NULL,&value_type,(LPBYTE)achValue,&cchMaxValue);
		if(ret==ERROR_SUCCESS&&_wcsicmp(achValue,L"Hotfix")==0)
		{
			return 1;
		}

		//if(info.ParentDisplayName!=L"")
		//	AfxMessageBox(info.ParentDisplayName.c_str());



		if(ins_loc==L"")
			ins_loc=__FindInstLoc(key);

		ins_loc.Trim('\\');
		if(PathIsDirectory(ins_loc)==FALSE&&ins_loc!=L""&&(ins_loc.Find(L"-")>=0||ins_loc.Find(L"\"")>=0))
		{
			int ct=0;
			LPWSTR* szArglist=CommandLineToArgvW(ins_loc.GetBuffer(),&ct);
			if(ct>0)
				ins_loc=szArglist[0];
			LocalFree(szArglist);
			if(PathIsDirectory(ins_loc)==FALSE)
			{
				ins_loc=ins_loc.Left(ins_loc.ReverseFind('\\'));
			}
			if(PathFileExists(ins_loc)==FALSE)
				ins_loc=L"";
			ins_loc.Trim('\\');
		}

		

		if(func==NULL)
			return 1;

		if(func(uid,disp_name,disp_icon,ins_loc,uni_cmd,disp_parent,para)==1)
			return 1;

		//UninstallString
	}


	return 0;
}

bool CSoftChecker::__hasAVP()
{
	return true;
	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof( PROCESSENTRY32 );

	HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

	if( !Process32First( snap, &pe32 ) )
	{
		CloseHandle( snap );     // Must clean up the snapshot object!
		return 0;
	}

	bool end=true;

	do
	{
		CString exe=pe32.szExeFile;
		exe.MakeLower();
		if(exe.Find(L"avp.exe")==0)
			return true;

	} while( Process32Next( snap, &pe32 ) );

	CloseHandle(snap);
	return false;
}


int CSoftChecker::UninstallHook( CString key,CString name,CString disp_icon,CString loc,CString uni_cmd,CString parent,void* param )
{
	if( name.IsEmpty() )
		return 0;

	UniHook* hook = (UniHook*)param;
	key.MakeLower();
/*
type	name	tbl_name	rootpage	sql
table	unin	unin	5	CREATE TABLE unin(k,name,ico,loc,uni,pr,cname,py_,pq_,pname,stat default '已安装',si default '',lastuse si default '',unique(k))
*/
	CString sql=L"insert or replace into unin(k,name,ico,loc,uni,pr,cname,py_,pq_,pname,stat) values(?,?,?,?,?,?,?,?,?,?,?)";
	const void *zLeftover=NULL; 
	sqlite3_stmt* st=NULL;
	
	// 插入一条记录, 这个函数仅当子键缓存过期时才调用
	int ret=sqlite3_prepare16_v2(hook->theOne->_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1, key.GetBuffer(),key.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2, name.GetBuffer(),name.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,3, disp_icon.GetBuffer(),disp_icon.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,4, loc.GetBuffer(),loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,5, uni_cmd.GetBuffer(),uni_cmd.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,6, parent.GetBuffer(),parent.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,7, hook->cache_name.GetBuffer(),hook->cache_name.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	
	// 软件的拼音
	//[to do] 加载拼音库也是耗时很长的
	CString			py,pq;

    if( mPinyin.size() > 0 )
	{
		CStringA		py_a,pq_a;
		py_a = CW2A(name).m_psz;
		
		GetPinyin(py_a,pq_a);
		py=CA2W(py_a).m_psz;
		pq=CA2W(pq_a).m_psz;

	}
	ret=sqlite3_bind_text16(st,8, py.GetBuffer(),py.GetLength()*sizeof TCHAR,SQLITE_TRANSIENT);
	ret=sqlite3_bind_text16(st,9, pq.GetBuffer(),pq.GetLength()*sizeof TCHAR,SQLITE_TRANSIENT);

	CString pname = RemoveVersionEtc(name);
	ret=sqlite3_bind_text16(st,10, pname.GetBuffer(),pname.GetLength()*sizeof TCHAR,SQLITE_TRANSIENT);


	ret=sqlite3_bind_text16(st,11, "",0,SQLITE_STATIC);
	//ret=sqlite3_bind_text16(st,12,"",0,SQLITE_STATIC);
	//ret=sqlite3_bind_text16(st,13,"",0,SQLITE_STATIC);
	
	// 执行
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);

	return hook->func ? hook->func(key,name,disp_icon,loc,uni_cmd,parent,hook->para) : 0;
}


// 检查子键的记录是否在缓存中
bool CSoftChecker::IsKeyCached( HKEY parent,CString sub,FILETIME& t )
{
	CString sql=L"select count(lastwrite) from cachetime where pr = ? and sub = ? and lastwrite = ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;

	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	
	// 指定参数
	ret=sqlite3_bind_blob(st,1,&parent,sizeof parent,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,sub.GetBuffer(),sub.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_blob(st,3,&t,sizeof t,SQLITE_STATIC);
	
	// 执行查询
	ret=sqlite3_step(st);
	bool re=( sqlite3_column_int(st,0) != 0 );
	
	ret=sqlite3_finalize(st);
	
	return re;
}


void CSoftChecker::RemoveCache(CString& key)
{
	CString sql=L"delete from unin where k = ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,key.GetBuffer(),key.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
}


void CSoftChecker::UpdateKeyCache( HKEY parent,CString sub,FILETIME& t )
{
	CString sql=L"delete from cachetime where pr = ? and sub = ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	
	// 删除子键的缓存记录
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_blob(st,1,&parent,sizeof parent,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,sub.GetBuffer(),sub.GetLength()*sizeof sub[0],SQLITE_STATIC);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
	
	// 新增此子键的缓存记录(HKEY, SubKey, WriteTime)
	sql=L"insert into cachetime values(?,?,?)";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_blob(st,1,&parent,sizeof parent,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,sub.GetBuffer(),sub.GetLength()*sizeof sub[0],SQLITE_STATIC);
	ret=sqlite3_bind_blob(st,3,&t,sizeof t,SQLITE_STATIC);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
}


// 查询子键的缓存, 获取软件的记录列表
int CSoftChecker::CheckAll2UnistallByCache(UniHook* para )
{
	// 若无回调，不查询缓存
	if( para->func == NULL )
		return 0;

	CString sql=L"select * from unin where cname = ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	
	// 查询子键的缓存, 获取软件的记录列表
	int ret = sqlite3_prepare16_v2(_cache, sql.GetBuffer(), -1, &st, &zLeftover);
	ret = sqlite3_bind_text16(st, 1, para->cache_name.GetBuffer(), para->cache_name.GetLength() * sizeof TCHAR, SQLITE_STATIC);
	while( true )
	{
		ret = sqlite3_step(st);
		if( ret != 100 || sqlite3_column_count(st) == 0 )
			break;
		
		re = para->func((LPCWSTR)sqlite3_column_text16(st,0),
			(LPCWSTR)sqlite3_column_text16(st,1),
			(LPCWSTR)sqlite3_column_text16(st,2),
			(LPCWSTR)sqlite3_column_text16(st,3),
			(LPCWSTR)sqlite3_column_text16(st,4),
			(LPCWSTR)sqlite3_column_text16(st,5),
			para->para);
		
		if(re == 1)
			break;

	}

	ret=sqlite3_finalize(st);	
	return re;
}

// 读取卸载缓存
int CSoftChecker::LoadUnInfo( CString lib_file )
{
	::SetThreadLocale( MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED) ); 

	//OutputDebugString(L"LoadSoftDB");
	CDataFileLoader	loader;
	BkDatLibHeader new_h;
	
	if(loader.GetLibDatHeader(lib_file,new_h)==FALSE)
		return 1;
	
	_uni_lib=lib_file;
	if(has_cache())
		return 0;

	TiXmlDocument plugins;
	BkDatLibContent cont;

	if(loader.GetLibDatContent(lib_file,cont)==FALSE)
		return 2;
	if(false==plugins.Parse((char*)cont.pBuffer))
		return 3;

	TiXmlHandle hDoc(&plugins);
	TiXmlElement* pElem;
	TiXmlHandle hroot(NULL);
	pElem=hDoc.FirstChildElement().Element();
	hroot=TiXmlHandle(pElem);

	pElem=hroot.FirstChildElement("sample").Element();
	for(pElem;pElem;pElem=pElem->NextSiblingElement("sample"))
	{
		for(TiXmlElement* pE_ps=pElem->FirstChildElement();pE_ps;pE_ps=pE_ps->NextSiblingElement())
		{
			const char* vvv=pE_ps->Value();
			const char* att=pE_ps->Attribute("tname");
			const char* cld=pE_ps->Attribute("child");

			if(att==NULL||cld==NULL)
				continue;

			CAtlArray<CString> atts;
			for(TiXmlElement* pE_att=pE_ps->FirstChildElement("att");pE_att;pE_att=pE_att->NextSiblingElement("att"))
			{
				const char* txt=pE_att->GetText();
				if(txt)
					atts.Add(CA2W(txt));
			}
			CAtlArray<CString> pros;
			for(TiXmlElement* pE_att=pE_ps->FirstChildElement("pro");pE_att;pE_att=pE_att->NextSiblingElement("pro"))
			{
				const char* txt=pE_att->GetText();
				if(txt)
					pros.Add(CA2W(txt));
			}

			//建表啦！！！
			sql_run(L"BEGIN;");

			CString sql=L"CREATE TABLE ";
			sql+=att;
			sql+=L"(";
			for (size_t i=0;i<atts.GetCount();i++)
			{
				if(i!=0)
					sql+=L",";
				sql+=atts[i];
			}
			for (size_t i=atts.GetCount();i-atts.GetCount()<pros.GetCount();i++)
			{
				if(i!=0)
					sql+=L",";
				sql+=pros[i-atts.GetCount()];
			}
			sql+=L")";

			sql_run(sql);

			//处理这个表！ 


			for(TiXmlElement* pE_t=hroot.FirstChildElement(vvv).Element();pE_t;pE_t=pE_t->NextSiblingElement(vvv))
			{
				CString name;
				CString value;
				CString sql_ins;
				for(TiXmlElement* pE_t_i=pE_t->FirstChildElement(cld);pE_t_i;pE_t_i=pE_t_i->NextSiblingElement(cld))
				{
					name=att;
					name+=L"(";
					value=L"values (";
					for (size_t i=0;i<atts.GetCount();i++)
					{
						const char* item=pE_t_i->Attribute(CW2A(atts[i]));
						if(item)
							AppendNV(name,value,CW2A(atts[i]),item,i==0?"":",");
					}
					for (size_t i=atts.GetCount();i-atts.GetCount()<pros.GetCount();i++)
					{
						const char* item=pE_t_i->FirstChildElement(CW2A(pros[i-atts.GetCount()]))->GetText();
						if(item)
							AppendNV(name,value,CW2A(pros[i-atts.GetCount()]),item,i==0?"":",");
					}
					name+=L")";
					value+=L")";
					sql_ins=L"insert into "+name +L" " +value;
					sql_run(sql_ins);
				}
			}
			sql_run(L"COMMIT;");

		}
	}
	//sql_run(L"create index ciindex on soft(softid);");
	return 0;
}

int CSoftChecker::LoadFonts(CString strFile)
{
	if( mPinyin.size() == 0 )
	{
		LoadFont(CW2A(strFile).m_psz);
	}

	return (mPinyin.size() == 0);
}

bool CSoftChecker::has_cache()
{
	if( PathFileExists(_cache_name) )
	{
		CDataFileLoader	loader;
		BkDatLibHeader lib_h;
		
		if(loader.GetLibDatHeader(_uni_lib, lib_h)==FALSE)
			return false;
		
		// 查询版本号
		int ret=0;
		const void *zLeftover; 
		BkDatLibHeader* ph=NULL;
		sqlite3_stmt* st=NULL;
		CString sql=L"select lib from header_t;";
		
		ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
		ret=sqlite3_step(st);
		ph=(BkDatLibHeader *)sqlite3_column_blob(st,0);
		if(ph == NULL)
			return false;

		// 检查版本号是否相同
		if( ph->llVersion.QuadPart == lib_h.llVersion.QuadPart )
		{
			ret=sqlite3_finalize(st);
			return true;
		}
		else if( ph->llVersion.QuadPart != lib_h.llVersion.QuadPart )
		{
			ret=sqlite3_finalize(st);
			FreeUnInfo();
			
			LoadUnInfo(_uni_lib);
			update_cache();
			return true;
		}
	}

	return false;
}

int CSoftChecker::AppendNV( CString& name,CString& val,const char* name_key,const char* name_att ,CString sep)
{
	if(name_att)
	{
		name+=sep;
		name+=name_key;
		val+=sep;
		val+="\"";
		val+=name_att;
		val+="\"";
	}
	return 0;
}

void CSoftChecker::update_cache()
{
	CDataFileLoader	loader;
	BkDatLibHeader lib_h;
	
	if(loader.GetLibDatHeader(_uni_lib,lib_h)==FALSE)
		return;
	
	int ret=0;
	sql_run(L"drop table header_t");
	sql_run(L"create table header_t(lib)");
	
	const void *zLeftover; 
	BkDatLibHeader* ph=NULL;
	sqlite3_stmt* st=NULL;
	CString sql=L"insert into header_t values (?)";
	
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_blob(st,1,&lib_h,sizeof lib_h,SQLITE_STATIC);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
}


wchar_t* CSoftChecker::GetNextItem( void* pos )
{
	sqlite3_stmt* st=(sqlite3_stmt*)pos;
	int ret=sqlite3_step(st);
	wchar_t* pp=(wchar_t*)sqlite3_column_text16(st,0);
	if(ret==SQLITE_DONE)
		return NULL;
	return (wchar_t*)sqlite3_column_text16(st,0);
}

void CSoftChecker::FinalizeGet( void* pos )
{
	sqlite3_stmt* st=(sqlite3_stmt*)pos;
	sqlite3_finalize(st);
}

void* CSoftChecker::SearchUninstallItem( CString word )
{
	word=L"%"+word+L"%";
	int ret=0;
	const void *zLeftover; 
	BkDatLibHeader* ph=NULL;
	sqlite3_stmt* st=NULL;
	CString sql=L"select name,length(name) as n_l,name like ? as n_ma,py_ like ?1 as py_ma,pq_ like ?1 as pq_ma from unin ";
	sql+=L" where n_ma or py_ma or pq_ma";
	sql+=L" group by name,n_ma , py_ma , pq_ma ";
	sql+=L" order by  n_l asc,n_ma desc, py_ma desc, pq_ma desc";

	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,word.GetBuffer(),word.GetLength() *sizeof TCHAR,SQLITE_TRANSIENT);
	return st;
}

int CSoftChecker::FreeUnInfo()
{
	sql_run(L"drop table soft");
	sql_run(L"drop table header_t");
	return 0;
}

CString CSoftChecker::RemoveVersionEtc(CString& disp_name)
{
	CString name=disp_name+L" ";
	name.MakeLower();

	CAtlRegExp<> re;
	CAtlREMatchContext<> mc;
	re.Parse(L"{(V|v)?\\d+\\.\\d+\\.?\\d*\\.?\\d*}");
	while(TRUE==re.Match(name,&mc))
	{
		//mc.GetMatch)
		for (UINT nGroupIndex = 0; nGroupIndex < mc.m_uNumGroups;
			++nGroupIndex)
		{
			const CAtlREMatchContext<>::RECHAR* szStart = 0;
			const CAtlREMatchContext<>::RECHAR* szEnd = 0;
			mc.GetMatch(nGroupIndex, &szStart, &szEnd);
			CString a;
			a.Format(L"%.*s",szEnd-szStart,szStart);
			name.Replace(a,L"");
		}
	}


	const wchar_t* torep[]={
		L"卸载",L"测试",L"公测",L"内测",L"正式",L"优化",L"美化",L"合作",
		L"修改版",L"英文版",L"专业版",L"体验版",L"正式版",
		L"(R)",L"(C)",L"兼容包",L"uninstall",L"install",L"beta",L" - ",
		L"remove only",
		L"简体中文",L"版",L"x86",L"x64",L"(TM)"
		L"播放器",L"(",L"（",L"）",L")",L"仅移除",L"汉化",
		L"trial",L"共享版",L"免费版",L"bit",
		L" ",//这里不是空格,
		L"™",L"℠",L"®",L"Ⓡ",L"ⓡ",L"ⓒ",L"Ⓒ",L"©"
	};

	for (int i=0;i<sizeof torep/sizeof torep[0];i++)
	{
		name.Replace(torep[i],L" ");
	}


	//if(name.Find(L"kb")>0)
	//	__asm int 3;

	re.Parse(L"{(kb)?\\d+}");
	while(TRUE==re.Match(name,&mc))
	{
		//mc.GetMatch)
		for (UINT nGroupIndex = 0; nGroupIndex < mc.m_uNumGroups;
			++nGroupIndex)
		{
			const CAtlREMatchContext<>::RECHAR* szStart = 0;
			const CAtlREMatchContext<>::RECHAR* szEnd = 0;
			mc.GetMatch(nGroupIndex, &szStart, &szEnd);
			CString a;
			a.Format(L"%.*s",szEnd-szStart,szStart);
			name.Replace(a,L"");
		}
	}

	while(name.Replace(L"  ",L" "));
	name.Replace(L" ",L"%");
	return name;
}


void CSoftChecker::__GetDel(CString act,CString type, TiXmlElement* pRt, CAtlMap<CString,TiXmlElement*> &ids )
{
	for (TiXmlElement* pE=pRt->FirstChildElement(CW2A(act));pE;pE=pE->NextSiblingElement())
	{
		for (TiXmlElement* pDel_soft=pE->FirstChildElement(CW2A(type));pDel_soft;pDel_soft=pDel_soft->NextSiblingElement())
		{
			const char* id=pDel_soft->GetText();
			CString a=id;
			if(id)
			{
				wchar_t* del;
				wchar_t* tok=wcstok_s(a.GetBuffer(),L",",&del);
				while(tok)
				{
					ids.SetAt(tok,NULL);
					tok=wcstok_s(NULL,L",",&del);
				}
			}
		}
	}

}

void CSoftChecker::__merge( TiXmlElement* el, CString att, CAtlMap<CString,TiXmlElement*>* filter, TiXmlElement* re )
{
	for (TiXmlElement* pNewItem=el->FirstChildElement();pNewItem;pNewItem=pNewItem->NextSiblingElement())
	{

		const char* patt=pNewItem->Attribute(CW2A(att));
		if(patt&&filter->Lookup(CA2W(patt))==false)
		{
			re->LinkEndChild(pNewItem->Clone());
		}
	}
}

int CSoftChecker::Combine_UniInfo( CString diff_file )
{
	::SetThreadLocale( MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED) ); 

	CDataFileLoader	loader;
	BkDatLibHeader old_h;
	BkDatLibHeader new_h;
	
	if(_uni_lib==L"")
	{
		CString lib;
		PathCombine(lib.GetBuffer(1024),diff_file,L"..\\SoftUninst.dat");
		_uni_lib=lib;
	}
	
	if(loader.GetLibDatHeader(diff_file,new_h)==FALSE
		||loader.GetLibDatHeader(_uni_lib,old_h)==FALSE
		||old_h.llVersion.QuadPart!=new_h.llUpdateForVer.QuadPart)
		return 1;

	BkDatLibContent old_c;
	BkDatLibContent new_c;
	if(loader.GetLibDatContent(diff_file,new_c)==FALSE
		||loader.GetLibDatContent(_uni_lib,old_c)==FALSE)
		return 1;


	TiXmlDocument plugins;
	/*if(false==plugins.LoadFile(CW2A(_libfile)))
		return 1;*/
	TiXmlDocument diffs;
	//if(false==diffs.LoadFile(CW2A(diff_file)))
	//	return 1;
	if(plugins.Parse((char*)old_c.pBuffer)==false||diffs.Parse((char*)new_c.pBuffer)==false)
		return 2;

	TiXmlDocument newlib;
	TiXmlDeclaration* dec=new TiXmlDeclaration("1.0","gbk","");
	newlib.LinkEndChild(dec);
	TiXmlElement* rt=new TiXmlElement("allsoft");
	newlib.LinkEndChild(rt);

	TiXmlElement* pRt_old=plugins.RootElement();
	TiXmlElement* pRt_new=diffs.RootElement();

	TiXmlElement* pE=pRt_new->FirstChildElement("sample");
	rt->LinkEndChild(pE?pE->Clone():pRt_old->FirstChildElement("sample")->Clone());
	pE=pRt_new->FirstChildElement("softwares");
	rt->LinkEndChild(pE?pE->Clone():pRt_old->FirstChildElement("softwares")->Clone());

	CAtlMap<CString,TiXmlElement*> types;
	CAtlMap<CString,TiXmlElement*> softs;

	 __GetDel("del","softwares", pRt_new, softs);
	 //__GetDel("del","softtype", pRt_new, types);
	 __GetDel("modify","softwares", pRt_new, softs);
	 //__GetDel("modify","softtype", pRt_new, types);

	 CString type=L"softwares";
	 CString att=L"softid";
	 TiXmlElement* el=pRt_old->FirstChildElement(CW2A(type));
	 TiXmlElement* re=rt->FirstChildElement(CW2A(type));
	 if(re==NULL)
	 {
		 re=new TiXmlElement(CW2A(type));
		 rt->LinkEndChild(re);
	 }
	 CAtlMap<CString,TiXmlElement*>* filter=&softs;
	 __merge(el, att, filter, re);
	 /*filter=&types;
	 type=L"softtype";
	 att=L"id";
	 el=pRt_old->FirstChildElement(CW2A(type));
	 re=rt->FirstChildElement(CW2A(type));
	 if(re==NULL)
	 {
		 re=new TiXmlElement(CW2A(type));
		 rt->LinkEndChild(re);
	 }
	 __merge(el, att, filter, re);*/

	 TiXmlPrinter printer;
	 printer.SetIndent( "\t" );
	 newlib.Accept( &printer );

	 BkDatLibEncodeParam	paramx(enumLibTypePlugine,new_h.llVersion,(BYTE*)printer.CStr(),(DWORD)printer.Size(),1);
	 loader.Save(_uni_lib,paramx);


	return 0;
}

int CSoftChecker::CheckAll2UninstallByType( SoftType tp,UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	// 有缓存
	CheckAll2Uninstall(NULL,NULL);

#ifdef _DEBUG
	{
		CString n;
		n.Format(L"CheckAll2UninstallByType");
		OutputDebugString(n);
	}
#endif // _DEBUG

	sql_run(L"BEGIN;");
	
	if(has_cache()==false)
		update_cache();
	
	// 创建un_items，其中为有描述的软件
	sql_run(L"create view un_items as select *, (uninstallname like name) as rk from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");

	//sql_run(L"create view un_items as select * from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");
	//sql_run(L"create view un_items as select * from unin join soft on hide!='1' and (name=uninstallname) or (uninstallname=pname and matchtype='0')");

	// 根据类别获取软件列表
	switch(tp)
	{
	case PS_MODULE:
		__update_ps(L"ps_table");
		break;

	case STARTMENU:
		__update_startmenu(L"ps_table");
		break;

	case QUICKLAUNCH:
		__update_ql(L"ps_table");
		break;

	case DESKTOP:
		__update_desktop(L"ps_table");
		break;

	case NOTIFY_ICON:
		__update_notifyicon(L"ps_table");
		break;
	}
	
	//sql_run(L"create view un_items_view as Select * From un_items join ( select min(length(uninstallname)) as mrk,max(rk) as mxk,name as mname from un_items group by uni ) on mname=name where hide!='1' and (rk or (length(uninstallname)+mxk=mrk and rk=0)) group by uni");
	//sql_run(L"CREATE VIEW un_items_view AS SELECT * FROM un_items group by uni");
	sql_run(L"COMMIT;");
	
	// 查询获取的文件列表 ? 这里是根据安装目录来判断的，若目录被多个软件共享？
	//CString sql=L"select * from un_items_view join ps_table on length(loc)>3 and loc like substr(item,1,length(loc)) group by name,uni";
	CString sql=L"select * from un_items join ps_table on length(loc)>3 and loc like substr(item,1,length(loc)) group by name,uni";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(true)
	{
		// 查询太耗时了，大约一次要1s
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
#ifdef _DEBUG
		{
			CString n;
			n.Format(L"%d",ct);
			OutputDebugString(n);
		}
#endif // _DEBUG
		for (int i=0;i<ct;i++)
		{
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i)==CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);

	// 未在软件列表中的软件
	//sql=L"select * from (select * from unin where name not in (select name from un_items_view)) join ps_table on length(loc)>3 and loc like substr(item,1,length(loc)) group by name,uni";
	sql=L"select * from (select * from unin where name not in (select name from un_items)) join ps_table on length(loc)>3 and loc like substr(item,1,length(loc)) group by name,uni";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(true)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for (int i=0;i<ct;i++)
		{
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i)==CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);

	return re;
}


CString DosDevice2FilePath(CString& item)
{
	CString ret;
#define  BUFSIZE MAX_PATH
	TCHAR szTemp[BUFSIZE];
	szTemp[0] = '\0';

	if (GetLogicalDriveStrings(BUFSIZE-1, szTemp)) 
	{
        TCHAR szName[MAX_PATH] = { 0 };
		TCHAR szDrive[3] = TEXT(" :");
		BOOL bFound = FALSE;
		TCHAR* p = szTemp;

		do 
		{
			// Copy the drive letter to the template string
			*szDrive = *p;

			// Look up each device name
			if (QueryDosDevice(szDrive, szName, BUFSIZE))
			{
				UINT uNameLen = (UINT)_tcslen(szName);

				if (uNameLen < MAX_PATH) 
				{
					bFound = _tcsnicmp(item.GetBuffer(), szName, 
						uNameLen) == 0;

					if (bFound) 
					{
						//TCHAR szTempFile[MAX_PATH];
						ret.Format(TEXT("%s%s"),
							szDrive,
							item.GetBuffer()+uNameLen);
					}
				}
			}
			while (*p++);
		} while (!bFound && *p); // end of string
	}
	return ret;
}

void CSoftChecker::__update_ps( CString table_n )
{
	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof( PROCESSENTRY32 );

	HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

	if( !Process32First( snap, &pe32 ) )
	{
		CloseHandle( snap );     // Must clean up the snapshot object!
	}

	sql_run(L"drop table "+table_n);
	sql_run(L"create table "+ table_n +L"(item)");
	CString sql=L"insert into "+table_n+L" values (?)";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);

	bool end=true;

	do
	{
		CString exe=pe32.szExeFile;
		TCHAR buf[MAX_PATH]={0};
		HANDLE proc=OpenProcess(PROCESS_QUERY_INFORMATION ,FALSE,pe32.th32ProcessID);
		GetProcessImageFileName(proc,buf,MAX_PATH);
		CString item=buf;
		item=DosDevice2FilePath(item);

		//PathRemoveFileSpec(buf);
		if(buf[0]!=0)
		{
			ret=sqlite3_bind_text16(st,1,item.GetBuffer(),item.GetLength()*sizeof TCHAR,SQLITE_STATIC);
			ret=sqlite3_step(st);
			ret=sqlite3_reset(st);
		}
		CloseHandle(proc);
	} while( Process32Next( snap, &pe32 ) );
	ret=sqlite3_finalize(st);
	CloseHandle(snap);
}

void CSoftChecker::__update_lnk( CString table_n,CString dir )
{
	if(dir.IsEmpty())
		return;
	struct  
	{
		CSoftChecker* th;
		bool operator()(CString dir,_wfinddata_t& info)
		{
			if(info.name[0]=='.')
				return false;
			CString name=dir+L"\\"+info.name;
			if(info.attrib& _A_SUBDIR)
				ForeachFile(name,*this);
			else if(0==_wcsicmp(name.Right(3).GetBuffer(),L"lnk"))
				th->__get_lnk_path(name);
			return false;
		}
	}func={this};

	CString sql=L"update lnk set ex = '0' where name like '"+dir+L"%'";
	sql_run(sql);
	;
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=0;

	ForeachFile(dir,func);

	sql=L"delete from lnk where ex='0'";
	sql_run(sql);

	sql=L"create table "+ table_n +L" as select path as item from lnk where name like '"+dir+L"%'";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	if(ret!=0)
	{
		CAtlList<CString> items;
		sql=L"select path as item from lnk where name like '"+dir+L"%'";
		ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
		while(re!=1)
		{
			ret=sqlite3_step(st);
			if(ret!=100||sqlite3_column_count(st)==0)
				break;
			CString path;
			path=(wchar_t*)sqlite3_column_text16(st,0);
			int fund=0;
			if(path.IsEmpty()==FALSE&&IsSysDir(path)==false)
				items.AddTail(path);
		}
		ret=sqlite3_finalize(st);
		sql=L"insert into "+table_n+L" values (?)";
		ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
		while (items.GetHeadPosition())
		{
			ret=sqlite3_bind_text16(st,1,items.GetHead().GetBuffer(),items.GetHead().GetLength()*sizeof TCHAR,SQLITE_TRANSIENT);
			ret=sqlite3_step(st);
			ret=sqlite3_reset(st);
			items.RemoveHead();
		}
		ret=sqlite3_finalize(st);
	}
	else
	{
		ret=sqlite3_bind_text16(st,1,dir.GetBuffer(),dir.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		ret=sqlite3_step(st);
		ret=sqlite3_finalize(st);
	}


	/*sql_run(L"create VIEW "+ table_n +L"(item)");
	CString sql=L"insert into "+table_n+L" values (?)";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);

	CAtlList<CString> dirs;
	dirs.AddHead(dir);
	while (dirs.GetHeadPosition())
	{
		CEnumFile fs(dirs.GetHead(),L"*.*");
		for(int i=0;i<fs.GetDirCount();i++)
			dirs.AddTail(fs.GetDirFullPath(i));
		for(int i=0;i<fs.GetFileCount();i++)
		{
			CString ff=fs.GetFileFullPath(i);
			ff.MakeLower();
			if(ff.Right(3)!=L"lnk")
				continue;
			TCHAR buf[1024];
			GetLnkFullPath(ff,L"",buf);

			CString real_loc=buf;
			real_loc.MakeLower();
			if(real_loc.IsEmpty()==FALSE)
			{
				ret=sqlite3_bind_text16(st,1,real_loc.GetBuffer(),real_loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
				ret=sqlite3_step(st);
				ret=sqlite3_reset(st);
			}


		}
		dirs.RemoveHead();
	}
	ret=sqlite3_finalize(st);*/
}

void CSoftChecker::__update_desktop( CString table_n )
{
	TCHAR programs[MAX_PATH]={0};
	SHGetSpecialFolderPath(NULL,programs,CSIDL_DESKTOPDIRECTORY,FALSE);
	sql_run(L"drop table "+table_n);
	__update_lnk(table_n,programs);//CSIDL_COMMON_DESKTOPDIRECTORY
	SHGetSpecialFolderPath(NULL,programs,CSIDL_COMMON_DESKTOPDIRECTORY,FALSE);
	__update_lnk(table_n,programs);
}

void CSoftChecker::__update_startmenu( CString table_n )
{
	TCHAR programs[MAX_PATH]={0};
	SHGetSpecialFolderPath(NULL,programs,CSIDL_STARTMENU,FALSE);
	sql_run(L"drop table "+table_n);
	__update_lnk(table_n,programs);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_COMMON_STARTMENU,FALSE);
	__update_lnk(table_n,programs);
}

void CSoftChecker::__update_ql( CString table_n )
{
	TCHAR programs[MAX_PATH]={0};
	ZeroMemory(programs,MAX_PATH);
	SHGetSpecialFolderPath(NULL,programs,CSIDL_APPDATA,FALSE);
	wcscat_s(programs,MAX_PATH,L"\\Microsoft\\Internet Explorer\\Quick Launch");
	sql_run(L"drop table "+table_n);
	__update_lnk(table_n,programs);
}


void CSoftChecker::__update_notifyicon( CString table_n )
{
	// 重建新表
	sql_run(L"drop table "+table_n);
	sql_run(L"create table "+ table_n +L"(item)");
	
	CString sql=L"insert into "+table_n+L" values (?)";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	
	// 枚举文件名，保存到表中
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	try
	{
		HWND win=FindWindow(L"Shell_TrayWnd",NULL);
		HWND nwnd=FindWindowEx(win,NULL,L"TrayNotifyWnd",NULL);
		
		nwnd=FindWindowEx(nwnd,NULL,L"SysPager",NULL);
		
		HWND tray=FindWindowEx(nwnd,NULL,L"ToolbarWindow32",NULL);
		DWORD pid=0;
		
		GetWindowThreadProcessId(tray,&pid);
		
		HANDLE hProcess   =   OpenProcess(PROCESS_VM_OPERATION   |   PROCESS_VM_READ,   0,   pid)  ;//  |   PROCESS_VM_OPERATION   |   PROCESS_VM_READ
		if(hProcess==NULL)
			throw GetLastError();

		LPVOID lngAddress   =   VirtualAllocEx(hProcess,  0,   4096,   MEM_COMMIT,   PAGE_READWRITE) ;  
		if(lngAddress==NULL)
			throw GetLastError();
		
		LRESULT	lngButtons   =   SendMessage(tray,   TB_BUTTONCOUNT,   0,   0)  ;
		for (int i=0;i<lngButtons;i++)
		{
			LRESULT ret   =   SendMessage(tray,   TB_GETBUTTON,   i,   (LPARAM)lngAddress)  ;
			BYTE buff[4096]={0};
			SIZE_T dwSize;
			
			ReadProcessMemory(hProcess,lngAddress,buff,4096,&dwSize);
			PTBBUTTON ptb=(PTBBUTTON)buff;
			HWND hw;
			
			ReadProcessMemory(hProcess,(LPCVOID)ptb->dwData,&hw,sizeof hw,&dwSize);
			//PNOTIFYICONDATA pnd= (PNOTIFYICONDATA)buff;
			GetWindowThreadProcessId((HWND)hw,&pid);
			
			HANDLE hproc=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,pid);
			TCHAR name[MAX_PATH]={0};
			
			GetProcessImageFileName(hproc,name,MAX_PATH);
			
			CString item=name;
			item=DosDevice2FilePath(item);
			if(name[0]!=0)
			{
				ret=sqlite3_bind_text16(st,1,item.GetBuffer(),item.GetLength()*sizeof TCHAR,SQLITE_STATIC);
				ret=sqlite3_step(st);
				ret=sqlite3_reset(st);
			}
			int z=0;
		}
        //如果使用 MEM_RELEASE，则第三个参数必须是0
		VirtualFreeEx(hProcess,lngAddress,0,MEM_RELEASE);
	}
	catch (...)
	{
	}
	
	ret=sqlite3_finalize(st);
}

void CSoftChecker::SetItemStat( CString sta,CString name )
{
	CString sql=L"update unin set stat = ? where k like ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,sta.GetBuffer(),sta.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,name.GetBuffer(),name.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	ret=sqlite3_finalize(st);
}

CString CSoftChecker::__get_lnk_path( CString lnk )
{
	CString re;
	struct _stat sta;
	_wstat(lnk.GetBuffer(),&sta);
	if(__is_lnk_changed(lnk,&sta))
	{
		TCHAR buf[1024];
		GetLnkFullPath(lnk,L"",buf);
		re=buf;

		CString sql=L"insert or replace into lnk values (?,?,?,?,'1')";
		const void *zLeftover; 
		sqlite3_stmt* st=NULL;
		int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
		ret=sqlite3_bind_text16(st,1,lnk.GetBuffer(),lnk.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		ret=sqlite3_bind_text16(st,2,re.GetBuffer(),re.GetLength()*sizeof TCHAR,SQLITE_TRANSIENT);
		ret=sqlite3_bind_blob(st,3,&(sta.st_size),sizeof sta.st_size,SQLITE_STATIC);
		ret=sqlite3_bind_blob(st,4,&(sta.st_mtime),sizeof sta.st_mtime,SQLITE_STATIC);
		ret=sqlite3_step(st);
		ret=sqlite3_finalize(st);

		wcscpy_s(buf,1024,lnk.GetBuffer());
		PathRemoveFileSpec(buf);
		lnk=buf;
		if(IsSysDir(lnk)==false)
		{
			sql=L"insert or replace into lnk values (?,?,'','','1')";
			ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
			ret=sqlite3_bind_text16(st,1,lnk.GetBuffer(),lnk.GetLength()*sizeof TCHAR,SQLITE_TRANSIENT);
			ret=sqlite3_bind_text16(st,2,re.GetBuffer(),re.GetLength()*sizeof TCHAR,SQLITE_STATIC);
			ret=sqlite3_step(st);
			ret=sqlite3_finalize(st);
		}


		//ret=sqlite3_finalize(st);
		//sql=L"insert or replace into remains values (?,?,?,'')";
		//CString tp=L"文件";
		//CString loc;
		//PathRemoveFileSpec(buf);
		//loc=buf;
		//wcscpy_s(buf,1024,lnk.GetBuffer());
		//PathRemoveFileSpec(buf);
		//CString dir=buf;
		//ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
		//ret=sqlite3_bind_text16(st,1,tp.GetBuffer(),tp.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		//ret=sqlite3_bind_text16(st,2,lnk.GetBuffer(),lnk.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		//ret=sqlite3_bind_text16(st,3,loc.GetBuffer(),loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		//ret=sqlite3_step(st);
		//sqlite3_reset(st);
		//tp=L"目录";
		//ret=sqlite3_bind_text16(st,1,tp.GetBuffer(),tp.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		//ret=sqlite3_bind_text16(st,2,dir.GetBuffer(),dir.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		//ret=sqlite3_bind_text16(st,3,loc.GetBuffer(),loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		//ret=sqlite3_step(st);
		//ret=sqlite3_finalize(st);



	}
	CString sql=L"select path from lnk where name = ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,lnk.GetBuffer(),lnk.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	re=(wchar_t*)sqlite3_column_text16(st,0);
	ret=sqlite3_finalize(st);

	return re;

}

bool CSoftChecker::__is_lnk_changed( CString lnk,struct _stat* sta )
{
	CString sql=L"select count(*) from lnk where pr = ? and size = ? and last = ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,lnk.GetBuffer(),lnk.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_blob(st,3,&(sta->st_mtime),sizeof sta->st_mtime,SQLITE_STATIC);
	ret=sqlite3_bind_blob(st,2,&(sta->st_size),sizeof sta->st_size,SQLITE_STATIC);
	ret=sqlite3_step(st);
	bool re=(sqlite3_column_int(st,0)==0);
	ret=sqlite3_finalize(st);
	return re;
}

bool CSoftChecker::__is_lnk_remains( CString lnk )
{
	return PathFileExists(__get_lnk_path(lnk))!=FALSE;
}

__int64 CSoftChecker::CountSize( CString loc )
{
	if(loc.GetLength()<4)
		return -1;
	struct  
	{
		__int64 size;
		bool operator()(CString dir,_wfinddata_t& info)
		{
			if(info.name[0]=='.')
				return false;
			size+=info.size;
			if(info.attrib& _A_SUBDIR)
				ForeachFile(dir+L"\\"+info.name,*this);
			return false;
		}
	}func;

	//ForeachFile(argv[1],func);
	func.size=0;
	ForeachFile(loc,func);
	if(func.size!=0)
		__update_size(loc,func.size);
	return func.size;
}

int CSoftChecker::RemoveRemainds( CString name,CString ins_loc,CString type,CString loc,PowerSweepCallBack func,void* para )
{
	__temp.RemoveAll();

	int fund=0;
	
	__update_remains(type,loc,ins_loc,name);

	int re= __PowerSweep(type,loc,func,para,fund);
	if( name != L"" && ins_loc != L"" )
	{
		CString sql = L"update remains set name = ? where loc like ?";
		const void *zLeftover; 
		sqlite3_stmt* st=NULL;
		
		int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
		
		ret=sqlite3_bind_text16(st,1,name.GetBuffer(),name.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		ret=sqlite3_bind_text16(st,2,ins_loc.GetBuffer(),ins_loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
		
		sqlite3_step(st);
		sqlite3_finalize(st);
	}

	return re;
}

int CSoftChecker::RemoveLnk( SoftType tp,CString loc,PowerSweepCallBack func,void* para )
{
	__temp.RemoveAll();
	CString pa;
	loc+="%";
	int ci=0;
	TCHAR programs[MAX_PATH]={0};
	switch(tp)
	{
	case STARTMENU:
		ci=CSIDL_STARTMENU;
		SHGetSpecialFolderPath(NULL,programs,ci,FALSE);
		pa=programs+pa+L"%";
		__RemoveLnk(pa, loc, func, para);
		ci=CSIDL_COMMON_STARTMENU;
		pa=L"";
		SHGetSpecialFolderPath(NULL,programs,ci,FALSE);
		pa=programs+pa+L"%";
		__RemoveLnk(pa, loc, func, para);
		break;
	case DESKTOP:
		ci=CSIDL_DESKTOPDIRECTORY;
		SHGetSpecialFolderPath(NULL,programs,ci,FALSE);
		pa=programs+pa+L"%";
		OutputDebugString(L"桌面："+pa);
		__RemoveLnk(pa, loc, func, para);
		pa=L"";
		ci=CSIDL_COMMON_DESKTOPDIRECTORY;
		SHGetSpecialFolderPath(NULL,programs,ci,FALSE);
		pa=programs+pa+L"%";
		OutputDebugString(L"公共桌面："+pa);
		__RemoveLnk(pa, loc, func, para);
		break;
	case QUICKLAUNCH:
		ci=CSIDL_APPDATA;
		SHGetSpecialFolderPath(NULL,programs,ci,FALSE);
		pa=L"\\Microsoft\\Internet Explorer\\Quick Launch";
		pa=programs+pa+L"%";
		__RemoveLnk(pa, loc, func, para);
		break;
	default:
		;
	}
	return 0;
}

int CSoftChecker::CheckAll2Remains( UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	sql_run(L"BEGIN;");
	__update_desktop(L"ps_table");
	__update_startmenu(L"ps_table");
	__update_ql(L"ps_table");
	sql_run(L"COMMIT;");
	CString sql=L"select name,path from lnk where path like '%:%' ";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	CAtlMap<CString,CString> toberemove;
	CAtlMap<CString,CString> al;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		path=(wchar_t*)sqlite3_column_text16(st,1);
		if(PathFileExists(path))
			continue;
		path=(wchar_t*)sqlite3_column_text16(st,0);
		if(PathFileExists(path)==FALSE)
		{
			toberemove.SetAt(path,L"文件");
			continue;
		}
		if(al.Lookup(path))
			continue;
		al.SetAt(path,L"文件");
		if(cbfun)
		{
			cbfun(mp,L"type",L"文件");
			cbfun(mp,L"path",path.GetBuffer());
		}
		if(func)
			re=func(mp,para);
		//re=RemoveRemainds(L"文件",path,func,para);
	}
	ret=sqlite3_finalize(st);




	sql=L"select type,path,loc,name from remains where path like '%:%' ";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		int ct=sqlite3_column_count(st);
		CString type=(wchar_t*)sqlite3_column_text16(st,0);
		path=(wchar_t*)sqlite3_column_text16(st,1);
		if(PathFileExists(path)==FALSE)
		{
			toberemove.SetAt(path,type);
			continue;
		}
		if(al.Lookup(path))
			continue;
		al.SetAt(path,L"文件");
		if(cbfun)
		{
			for (int i=0;i<ct;i++)
			{
				cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
			}
		}
		if(func)
			re=func(mp,para);
		//re=RemoveRemainds(L"文件",path,func,para);
	}
	ret=sqlite3_finalize(st);

	for (POSITION p=toberemove.GetStartPosition();p;toberemove.GetNext(p))
	{
		__remove_remains(toberemove.GetValueAt(p),toberemove.GetKeyAt(p));
	}

	return 0;
}


// 创建卸载缓存
int CSoftChecker::MakeAll2UninstallCache()
{
	::EnterCriticalSection(&m_csUninCache);

	sql_run(L"drop view un_items");
	sql_run(L"create view un_items as select *, (uninstallname like name) as rk from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");
	//sql_run(L"create view un_items as select * from unin join soft on (matchtype='1' and name=uninstallname ) or ( matchtype='0' and (name like uninstallname or uninstallname like pname) ) ");
	__update_lastuse();
	sql_run(L"drop table un_items_cache");

	sql_run(L"create table un_items_cache as Select * From un_items join ( select min(length(uninstallname)) as mrk,max(rk) as mxk,name as mname from un_items group by uni ) on mname=name where hide!='1' and (rk or (length(uninstallname)+mxk=mrk and rk=0)) group by uni");

	sql_run(L"create index sname on un_items_cache(name)");
	sql_run(L"create index spname on un_items_cache(pname)");

	//sql_run(L"create table un_items_cache as select * from un_items group by uni");
	
	//sql_run(L"create index sname on un_items_cache(name)");

	::LeaveCriticalSection(&m_csUninCache);
	return 0;
}

//获取升级缓存
int CSoftChecker::GetUpdateFromCache(pfnUpdateCheckCallBack pfnCallBack, GetInfoCallback cbfun, void* mp, void* para)
{
	CString _strQuery = L"select * from update_cache";
	const void *zLeftover; 
	sqlite3_stmt* st = NULL;
	int re = 0;

	int ret=sqlite3_prepare16_v2(m_pUpdateDB,_strQuery.GetBuffer(),-1,&st,&zLeftover);
	_strQuery.ReleaseBuffer();
	while(true)
	{
		ret = sqlite3_step(st);
		if( ret != 100 || sqlite3_column_count(st) == 0 )
			break;

		int ct = sqlite3_column_count(st);
		for (int i=0;i<ct;i++)
		{
			re=0;
			if ( cbfun )
			{
				cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
			}
		}

		re=pfnCallBack(mp,para);
		if(re==1)
			break;
	}

	ret=sqlite3_finalize(st);
	return 0;
}

//建立升级缓存, 界面做了异步检测处理, 这里就不再做异步保存了, 但结构不好
int CSoftChecker::MakeUpdateCache()
{
	sql_run_upd(L"drop table update_cache");
	sql_run_upd(L"create table update_cache(id, name, cver, nver, lastupdate default '', unique(id))");
	sql_run_upd(L"create index id_index on update(id)");	//建立 id 索引

	//插入数据
	CString _sqlExec;
	CString _strTemp;
	const void *zLeftover; 
	sqlite3_stmt* st = NULL;
	int ret = 1;

	t_mapUpdateSoft _mapSoft = m_stUpdateMgr.GetUpdMgr();
	for ( t_iterUpdateSoft _iter = _mapSoft.begin(); _iter != _mapSoft.end(); ++_iter )
	{
		if ( !_iter->second ) continue;

		_sqlExec=L"insert into update_cache(id, name, cver, nver, lastupdate) values(?,?,?,?,?)";
		ret=sqlite3_prepare16_v2(m_pUpdateDB,_sqlExec.GetBuffer(),-1,&st,&zLeftover);
		_sqlExec.ReleaseBuffer();

		ret=sqlite3_bind_text16(st, 1, _iter->second->id, _iter->second->id.GetLength() * sizeof(wchar_t), SQLITE_STATIC);
		ret=sqlite3_bind_text16(st, 2, _iter->second->name, _iter->second->name.GetLength() * sizeof(wchar_t), SQLITE_STATIC);
		ret=sqlite3_bind_text16(st, 3, _iter->second->m_strCurVer, _iter->second->m_strCurVer.GetLength() * sizeof(wchar_t), SQLITE_STATIC);
		ret=sqlite3_bind_text16(st, 4, _iter->second->ver, _iter->second->ver.GetLength() * sizeof(wchar_t), SQLITE_STATIC);
		ret=sqlite3_bind_text16(st, 5, _iter->second->last_update, _iter->second->last_update.GetLength() * sizeof(wchar_t), SQLITE_STATIC);
		ret=sqlite3_step(st);
	}

	sqlite3_finalize(st);

	return 0;
}

int CSoftChecker::CheckAll2UninstallCache( UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	__update_lastuse();

	::EnterCriticalSection(&m_csUninCache);

	CString sql=L"select * From un_items_cache";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=1;
	
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	if ( ret != 0 )
	{
		sqlite3_finalize(st);
		re = 1;
		goto Exit0;
	}

	while(true)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for (int i=0;i<ct;i++)
		{
			re=0;
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i)==CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);
	
	sql=L"select * from unin where name not in (select name from un_items_cache) ";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(true)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;

		int ct=sqlite3_column_count(st);
		sql=L"";
		for (int i=0;i<ct;i++)
		{
			re=0;
			//sql=(wchar_t*)sqlite3_column_name16(st,i);
			//if((wchar_t*)sqlite3_column_name16(st,i)==CString(L"brief"))
			//	sql=(wchar_t*)sqlite3_column_text16(st,i);
			
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		
		re=func(mp,para);
		if(re==1)
			break;
	}
	ret=sqlite3_finalize(st);

	re = 0;
Exit0:
	::LeaveCriticalSection(&m_csUninCache);
	return re;
}

bool CSoftChecker::__update_remains( CString type,CString path,CString loc,CString name )
{
	CString sql=L"select count(path) from remains where type like ? and path like ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,type.GetBuffer(),type.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,path.GetBuffer(),path.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	re=sqlite3_column_int(st,0);
	sqlite3_finalize(st);

	CString val=loc;
	sql=L"insert or replace into remains(type,path,loc,name) values(?,?,?,?)";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,type.GetBuffer(),type.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,path.GetBuffer(),path.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,3,val.GetBuffer(),val.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,4,name.GetBuffer(),name.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	sqlite3_finalize(st);
	re=1;

	return re!=0;
}

bool CSoftChecker::__remove_remains( CString type,CString path )
{
	CString sql=L"delete from remains where type like ? and path like ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,type.GetBuffer(),type.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,path.GetBuffer(),path.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	sqlite3_finalize(st);


	sql=L"delete from lnk where name like ?";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,path.GetBuffer(),path.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_step(st);
	sqlite3_finalize(st);

	return re==0;
}

void CSoftChecker::__update_size( CString loc,__int64 size )
{
	if(loc==L"")
		return;
	CString sql;
	sql.Format(L"update unin set si='%I64d' where loc ='%s' ",size,loc.GetBuffer());
	sql_run(sql);

	TCHAR tempdir[MAX_PATH]={0};
	GetModuleFileName(NULL,tempdir,MAX_PATH);
	PathRemoveFileSpec(tempdir);
	CString _db_name=tempdir;
	_db_name+=L"\\AppData\\startup.log";
	CString sss;
	sss.Format(L"%I64d",size);
	WritePrivateProfileString(loc,L"size",sss.GetBuffer(),_db_name);

}

#include <sys/stat.h>

void CSoftChecker::__update_lastuse()
{
	TCHAR tempdir[MAX_PATH]={0};

	GetModuleFileName(NULL,tempdir,MAX_PATH);
	PathRemoveFileSpec(tempdir);
	
	CString _db_name=tempdir;
	_db_name+=L"\\AppData\\startup.log";
	wchar_t* names;
	struct _stat _st={0};
	_wstat(_db_name.GetBuffer(),&_st);
	DWORD size=_st.st_size;
	names=new wchar_t[size+2];
	ZeroMemory(names,4);
	if(names==NULL)
		return;
	
	sql_run(L"BEGIN;");
	GetPrivateProfileSectionNames(names,size,_db_name);
	CString sql=L"update unin set lastuse=? where length(loc)>4 and loc like substr(?,1,length(loc))";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	wchar_t* pnames=names;
	
	while(pnames[0])
	{
		wchar_t buf[MAX_PATH]={0};
		GetPrivateProfileString(pnames,L"lastuse",L"",buf,MAX_PATH,_db_name);
		if(_wcsicmp(buf,L"")!=0)
		{
			ret=sqlite3_bind_text16(st,1,buf,(int)wcslen(buf)*sizeof TCHAR,SQLITE_STATIC);
			ret=sqlite3_bind_text16(st,2,pnames,(int)wcslen(pnames)*sizeof TCHAR,SQLITE_STATIC);
			ret=sqlite3_step(st);
			ret=sqlite3_reset(st);
		}
		while(pnames++[0]);
	}
	sqlite3_finalize(st);

	sql=L"update unin set si=? where length(loc)>4 and loc like substr(?,1,length(loc))";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	pnames=names;
	while(pnames[0])
	{
		wchar_t buf2[MAX_PATH]={0};
		GetPrivateProfileString(pnames,L"size",L"",buf2,MAX_PATH,_db_name);
		if(_wcsicmp(buf2,L"")!=0)
		{
			ret=sqlite3_bind_text16(st,1,buf2,(int)wcslen(buf2)*sizeof TCHAR,SQLITE_STATIC);
			ret=sqlite3_bind_text16(st,3,pnames,(int)wcslen(pnames)*sizeof TCHAR,SQLITE_STATIC);
			ret=sqlite3_step(st);
			ret=sqlite3_reset(st);
		}
		while(pnames++[0]);
	}
	sqlite3_finalize(st);
	
	delete [] names;
	sql_run(L"COMMIT;");
}


int CSoftChecker::CheckAll2RemainsByGroup( UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	CheckAll2Remains(NULL,NULL,NULL,NULL);

	CString sql=L"select name,path from lnk where path like '%:%' ";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		path=(wchar_t*)sqlite3_column_text16(st,1);
		if(PathFileExists(path))
			continue;
		path=(wchar_t*)sqlite3_column_text16(st,0);
		cbfun(mp,L"type",L"文件");
		cbfun(mp,L"path",path.GetBuffer());
		re=func(mp,para);
		//re=RemoveRemainds(L"文件",path,func,para);
	}
	ret=sqlite3_finalize(st);

	sql=L"select type,loc as path,loc,name from remains where path like '%:%' group by name,loc";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		int ct=sqlite3_column_count(st);
		for (int i=0;i<ct;i++)
		{
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		re=func(mp,para);
		//re=RemoveRemainds(L"文件",path,func,para);
	}
	ret=sqlite3_finalize(st);
	return 0;
}

int CSoftChecker::CheckAll2RemainsByNameAndLoc( CString name,CString loc,UniCheckCallBackEx func,GetInfoCallback cbfun,void* mp,void* para )
{
	CString sql=L"select name,path from lnk where path like '%:%' ";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	if(loc==L"")
	{
		while(re!=1)
		{
			ret=sqlite3_step(st);
			if(ret!=100||sqlite3_column_count(st)==0)
				break;
			CString path;
			path=(wchar_t*)sqlite3_column_text16(st,1);
			if(PathFileExists(path))
				continue;
			path=(wchar_t*)sqlite3_column_text16(st,0);
			cbfun(mp,L"type",L"文件");
			cbfun(mp,L"path",path.GetBuffer());
			re=func(mp,para);
			//re=RemoveRemainds(L"文件",path,func,para);
		}
	}
	ret=sqlite3_finalize(st);

	sql=L"select type,path,loc,name from remains where path like '%:%' and name like ? and loc like ?";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,name.GetBuffer(),name.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,loc.GetBuffer(),loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		int ct=sqlite3_column_count(st);
		for (int i=0;i<ct;i++)
		{
			cbfun(mp,(wchar_t*)sqlite3_column_name16(st,i),(wchar_t*)sqlite3_column_text16(st,i));
		}
		re=func(mp,para);
		//re=RemoveRemainds(L"文件",path,func,para);
	}
	ret=sqlite3_finalize(st);
	return 0;
}

void CSoftChecker::__RemoveLnk( CString &pa, CString &loc, PowerSweepCallBack func, void* para )
{
	if(pa==L"%"||loc==L"%")
		return;
	CString sql=L"select name from lnk where name like ? and path like ?";
	const void *zLeftover; 
	sqlite3_stmt* st=NULL;
	int re=0;
	int ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	ret=sqlite3_bind_text16(st,1,pa.GetBuffer(),pa.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,2,loc.GetBuffer(),loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		path=(wchar_t*)sqlite3_column_text16(st,0);
		int fund=0;
		re=__PowerSweep(L"文件",path,func,para,fund);
	}
	ret=sqlite3_finalize(st);
	//快捷方式目录
	sql=L"select path from remains where path like ? ";
	ret=sqlite3_prepare16_v2(_cache,sql.GetBuffer(),-1,&st,&zLeftover);
	//ret=sqlite3_bind_text16(st,1,pa.GetBuffer(),pa.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	ret=sqlite3_bind_text16(st,1,loc.GetBuffer(),loc.GetLength()*sizeof TCHAR,SQLITE_STATIC);
	while(re!=1)
	{
		ret=sqlite3_step(st);
		if(ret!=100||sqlite3_column_count(st)==0)
			break;
		CString path;
		path=(wchar_t*)sqlite3_column_text16(st,0);
		int fund=0;
		re=__PowerSweep(L"目录",path,func,para,fund);
	}
	ret=sqlite3_finalize(st);
}

inline bool IsKeyDirectory(const CStringW &path)
{
	try
	{
		path_w dir((LPCWSTR)path);
		dir.canonicalise(true);

		// 根目录
		if(dir.size() <= 3) 
			return true;

		// 关键目录
		const static LPCWSTR spKey[] =
		{
			L"windir",
			L"SystemRoot",
			L"ProgramFiles",
			L"ProgramData",
			L"CommonProgramFiles",
		};

		wchar_t buffer[MAX_PATH];
		for(int i = 0; i < STLSOFT_NUM_ELEMENTS(spKey); ++i)
		{
			DWORD sz = ::GetEnvironmentVariableW(spKey[i], buffer, MAX_PATH);
			if(sz == 0 || sz >= MAX_PATH) 
				continue;

			if(buffer[sz - 1] == L'\\' || buffer[sz - 1] == L'/') 
				buffer[sz - 1] = L'\0';

			if(dir.equal(buffer))
				return true;
		}

		// System32
		UINT ui = ::GetSystemDirectoryW(buffer, MAX_PATH);
		if(ui == 0 || ui >= MAX_PATH)
			return false;

		if(buffer[ui - 1] == L'\\' || buffer[ui - 1] == L'/')
			buffer[ui - 1] = L'\0';

		if(dir.equal(buffer))
			return true;
	}
	catch(...) {}

	return false;
}