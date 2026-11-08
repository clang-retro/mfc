// dbdaoint.h - the interfaces of DAO, the Jet database engine's object
// model, as the DAO classes use them (the Unicode ones). There is no Jet
// engine outside Windows, so on other systems creating the engine fails
// and the DAO classes report that.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_DBDAOINT_H
#define _WINPORT_DBDAOINT_H

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <dbdaoid.h>

interface DAOError;
interface DAOErrors;
interface DAOProperty;
interface DAOProperties;
interface DAOField;
interface DAOFields;
interface DAOIndex;
interface DAOIndexes;
interface DAOIndexFields;
interface DAOParameter;
interface DAOParameters;
interface DAORecordset;
interface DAOQueryDef;
interface DAOQueryDefs;
interface DAOTableDef;
interface DAOTableDefs;
interface DAORelation;
interface DAORelations;
interface DAODatabase;
interface DAODatabases;
interface DAOWorkspace;
interface DAOWorkspaces;
interface DAODBEngine;

// Every DAO object has properties; collections count and enumerate.
interface _DAO : public IDispatch {
  STDMETHOD(get_Properties)(DAOProperties** ppprops) = 0;
};
interface _DAOCollection : public IDispatch {
  STDMETHOD(get_Count)(short* c) = 0;
  STDMETHOD(_NewEnum)(IUnknown** ppunk) = 0;
  STDMETHOD(Refresh)() = 0;
};
interface _DAODynaCollection : public _DAOCollection {
  STDMETHOD(Append)(IDispatch* Object) = 0;
  STDMETHOD(Delete)(BSTR Name) = 0;
};

interface DAOError : public IDispatch {
  STDMETHOD(get_Number)(LONG* pNumber) = 0;
  STDMETHOD(get_Source)(BSTR* pSource) = 0;
  STDMETHOD(get_Description)(BSTR* pDescription) = 0;
  STDMETHOD(get_HelpFile)(BSTR* pHelpFile) = 0;
  STDMETHOD(get_HelpContext)(LONG* pHelpContext) = 0;
};
interface DAOErrors : public _DAOCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOError** ppItem) = 0;
};
interface DAOProperty : public _DAO {
  STDMETHOD(get_Value)(VARIANT* pValue) = 0;
  STDMETHOD(put_Value)(VARIANT Value) = 0;
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_Type)(short* pType) = 0;
  STDMETHOD(put_Type)(short Type) = 0;
  STDMETHOD(get_Inherited)(VARIANT_BOOL* pInherited) = 0;
};
interface DAOProperties : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOProperty** ppItem) = 0;
};
interface DAOField : public _DAO {
  STDMETHOD(get_CollatingOrder)(LONG* pCollatingOrder) = 0;
  STDMETHOD(get_Type)(short* pType) = 0;
  STDMETHOD(put_Type)(short Type) = 0;
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_Size)(LONG* pSize) = 0;
  STDMETHOD(put_Size)(LONG Size) = 0;
  STDMETHOD(get_SourceField)(BSTR* pSourceField) = 0;
  STDMETHOD(get_SourceTable)(BSTR* pSourceTable) = 0;
  STDMETHOD(get_Value)(VARIANT* pValue) = 0;
  STDMETHOD(put_Value)(VARIANT Value) = 0;
  STDMETHOD(get_Attributes)(LONG* pAttributes) = 0;
  STDMETHOD(put_Attributes)(LONG Attributes) = 0;
  STDMETHOD(get_OrdinalPosition)(short* pOrdinalPosition) = 0;
  STDMETHOD(put_OrdinalPosition)(short OrdinalPosition) = 0;
  STDMETHOD(get_ValidationText)(BSTR* pValidationText) = 0;
  STDMETHOD(put_ValidationText)(BSTR ValidationText) = 0;
  STDMETHOD(get_ValidationOnSet)(VARIANT_BOOL* pValidationOnSet) = 0;
  STDMETHOD(put_ValidationOnSet)(VARIANT_BOOL ValidationOnSet) = 0;
  STDMETHOD(get_ValidationRule)(BSTR* pValidationRule) = 0;
  STDMETHOD(put_ValidationRule)(BSTR ValidationRule) = 0;
  STDMETHOD(get_DefaultValue)(VARIANT* pDefaultValue) = 0;
  STDMETHOD(put_DefaultValue)(VARIANT DefaultValue) = 0;
  STDMETHOD(get_Required)(VARIANT_BOOL* pRequired) = 0;
  STDMETHOD(put_Required)(VARIANT_BOOL Required) = 0;
  STDMETHOD(get_AllowZeroLength)(VARIANT_BOOL* pAllowZeroLength) = 0;
  STDMETHOD(put_AllowZeroLength)(VARIANT_BOOL AllowZeroLength) = 0;
  STDMETHOD(get_DataUpdatable)(VARIANT_BOOL* pDataUpdatable) = 0;
  STDMETHOD(get_ForeignName)(BSTR* pForeignName) = 0;
  STDMETHOD(put_ForeignName)(BSTR ForeignName) = 0;
  STDMETHOD(AppendChunk)(VARIANT Val) = 0;
  STDMETHOD(GetChunk)(LONG Offset, LONG Bytes, VARIANT* pChunk) = 0;
  STDMETHOD(CreateProperty)(VARIANT Name, VARIANT Type, VARIANT Value, VARIANT DDL, DAOProperty** ppProperty) = 0;
};
interface DAOFields : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOField** ppItem) = 0;
};
interface DAOIndex : public _DAO {
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_Foreign)(VARIANT_BOOL* pForeign) = 0;
  STDMETHOD(get_Unique)(VARIANT_BOOL* pUnique) = 0;
  STDMETHOD(put_Unique)(VARIANT_BOOL Unique) = 0;
  STDMETHOD(get_Clustered)(VARIANT_BOOL* pClustered) = 0;
  STDMETHOD(put_Clustered)(VARIANT_BOOL Clustered) = 0;
  STDMETHOD(get_Required)(VARIANT_BOOL* pRequired) = 0;
  STDMETHOD(put_Required)(VARIANT_BOOL Required) = 0;
  STDMETHOD(get_IgnoreNulls)(VARIANT_BOOL* pIgnoreNulls) = 0;
  STDMETHOD(put_IgnoreNulls)(VARIANT_BOOL IgnoreNulls) = 0;
  STDMETHOD(get_Primary)(VARIANT_BOOL* pPrimary) = 0;
  STDMETHOD(put_Primary)(VARIANT_BOOL Primary) = 0;
  STDMETHOD(get_DistinctCount)(LONG* pDistinctCount) = 0;
  STDMETHOD(get_Fields)(VARIANT* pFields) = 0;
  STDMETHOD(put_Fields)(VARIANT Fields) = 0;
  STDMETHOD(CreateField)(VARIANT Name, VARIANT Type, VARIANT Size, DAOField** ppField) = 0;
  STDMETHOD(CreateProperty)(VARIANT Name, VARIANT Type, VARIANT Value, VARIANT DDL, DAOProperty** ppProperty) = 0;
};
interface DAOIndexes : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOIndex** ppItem) = 0;
};
interface DAOIndexFields : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, VARIANT* pItem) = 0;
};
interface DAOParameter : public _DAO {
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(get_Value)(VARIANT* pValue) = 0;
  STDMETHOD(put_Value)(VARIANT Value) = 0;
  STDMETHOD(get_Type)(short* pType) = 0;
  STDMETHOD(put_Type)(short Type) = 0;
  STDMETHOD(get_Direction)(short* pDirection) = 0;
  STDMETHOD(put_Direction)(short Direction) = 0;
};
interface DAOParameters : public _DAOCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOParameter** ppItem) = 0;
};
interface DAORecordset : public _DAO {
  STDMETHOD(get_BOF)(VARIANT_BOOL* pBOF) = 0;
  STDMETHOD(get_Bookmark)(SAFEARRAY** ppsaBookmark) = 0;
  STDMETHOD(put_Bookmark)(SAFEARRAY** psaBookmark) = 0;
  STDMETHOD(get_Bookmarkable)(VARIANT_BOOL* pBookmarkable) = 0;
  STDMETHOD(get_DateCreated)(VARIANT* pDateCreated) = 0;
  STDMETHOD(get_EOF)(VARIANT_BOOL* pEOF) = 0;
  STDMETHOD(get_Filter)(BSTR* pFilter) = 0;
  STDMETHOD(put_Filter)(BSTR Filter) = 0;
  STDMETHOD(get_Index)(BSTR* pIndex) = 0;
  STDMETHOD(put_Index)(BSTR Index) = 0;
  STDMETHOD(get_LastModified)(SAFEARRAY** ppsaBookmark) = 0;
  STDMETHOD(get_LastUpdated)(VARIANT* pLastUpdated) = 0;
  STDMETHOD(get_LockEdits)(VARIANT_BOOL* pLockEdits) = 0;
  STDMETHOD(put_LockEdits)(VARIANT_BOOL LockEdits) = 0;
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(get_NoMatch)(VARIANT_BOOL* pNoMatch) = 0;
  STDMETHOD(get_Sort)(BSTR* pSort) = 0;
  STDMETHOD(put_Sort)(BSTR Sort) = 0;
  STDMETHOD(get_Transactions)(VARIANT_BOOL* pTransactions) = 0;
  STDMETHOD(get_Type)(short* pType) = 0;
  STDMETHOD(get_RecordCount)(LONG* pRecordCount) = 0;
  STDMETHOD(get_Updatable)(VARIANT_BOOL* pUpdatable) = 0;
  STDMETHOD(get_Restartable)(VARIANT_BOOL* pRestartable) = 0;
  STDMETHOD(get_ValidationText)(BSTR* pValidationText) = 0;
  STDMETHOD(get_ValidationRule)(BSTR* pValidationRule) = 0;
  STDMETHOD(get_CacheStart)(SAFEARRAY** pCacheStart) = 0;
  STDMETHOD(put_CacheStart)(SAFEARRAY* CacheStart) = 0;
  STDMETHOD(get_CacheSize)(LONG* pCacheSize) = 0;
  STDMETHOD(put_CacheSize)(LONG CacheSize) = 0;
  STDMETHOD(get_PercentPosition)(float* pPercentPosition) = 0;
  STDMETHOD(put_PercentPosition)(float PercentPosition) = 0;
  STDMETHOD(get_AbsolutePosition)(LONG* pAbsolutePosition) = 0;
  STDMETHOD(put_AbsolutePosition)(LONG AbsolutePosition) = 0;
  STDMETHOD(get_EditMode)(short* pEditMode) = 0;
  STDMETHOD(get_Fields)(DAOFields** ppFields) = 0;
  STDMETHOD(get_Indexes)(DAOIndexes** ppIndexes) = 0;
  STDMETHOD(CancelUpdate)(LONG UpdateType) = 0;
  STDMETHOD(AddNew)() = 0;
  STDMETHOD(Close)() = 0;
  STDMETHOD(OpenRecordset)(VARIANT Type, VARIANT Options, DAORecordset** ppRecordset) = 0;
  STDMETHOD(Delete)() = 0;
  STDMETHOD(Edit)() = 0;
  STDMETHOD(FindFirst)(BSTR Criteria) = 0;
  STDMETHOD(FindLast)(BSTR Criteria) = 0;
  STDMETHOD(FindNext)(BSTR Criteria) = 0;
  STDMETHOD(FindPrevious)(BSTR Criteria) = 0;
  STDMETHOD(MoveFirst)() = 0;
  STDMETHOD(MoveLast)(LONG Options) = 0;
  STDMETHOD(MoveNext)() = 0;
  STDMETHOD(MovePrevious)() = 0;
  STDMETHOD(Seek)(BSTR Comparison, VARIANT Key1, VARIANT Key2, VARIANT Key3, VARIANT Key4, VARIANT Key5, VARIANT Key6, VARIANT Key7, VARIANT Key8, VARIANT Key9, VARIANT Key10, VARIANT Key11, VARIANT Key12, VARIANT Key13) = 0;
  STDMETHOD(Clone)(DAORecordset** ppRecordset) = 0;
  STDMETHOD(Requery)(VARIANT NewQueryDef) = 0;
  STDMETHOD(Move)(LONG Rows, VARIANT StartBookmark) = 0;
  STDMETHOD(FillCache)(VARIANT Rows, VARIANT StartBookmark) = 0;
  STDMETHOD(GetRows)(VARIANT NumRows, VARIANT* pvar) = 0;
  STDMETHOD(Update)(LONG UpdateType, VARIANT_BOOL Force) = 0;
};
interface DAOQueryDef : public _DAO {
  STDMETHOD(get_DateCreated)(VARIANT* pDateCreated) = 0;
  STDMETHOD(get_LastUpdated)(VARIANT* pLastUpdated) = 0;
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_ODBCTimeout)(short* pODBCTimeout) = 0;
  STDMETHOD(put_ODBCTimeout)(short ODBCTimeout) = 0;
  STDMETHOD(get_Type)(short* pType) = 0;
  STDMETHOD(get_SQL)(BSTR* pSQL) = 0;
  STDMETHOD(put_SQL)(BSTR SQL) = 0;
  STDMETHOD(get_Updatable)(VARIANT_BOOL* pUpdatable) = 0;
  STDMETHOD(get_Connect)(BSTR* pConnect) = 0;
  STDMETHOD(put_Connect)(BSTR Connect) = 0;
  STDMETHOD(get_ReturnsRecords)(VARIANT_BOOL* pReturnsRecords) = 0;
  STDMETHOD(put_ReturnsRecords)(VARIANT_BOOL ReturnsRecords) = 0;
  STDMETHOD(get_RecordsAffected)(LONG* pRecordsAffected) = 0;
  STDMETHOD(get_Fields)(DAOFields** ppFields) = 0;
  STDMETHOD(get_Parameters)(DAOParameters** ppParameters) = 0;
  STDMETHOD(Close)() = 0;
  STDMETHOD(Execute)(VARIANT Options) = 0;
  STDMETHOD(CreateProperty)(VARIANT Name, VARIANT Type, VARIANT Value, VARIANT DDL, DAOProperty** ppProperty) = 0;
  STDMETHOD(OpenRecordset)(VARIANT Type, VARIANT Options, VARIANT LockEdit, DAORecordset** ppRecordset) = 0;
};
interface DAOQueryDefs : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOQueryDef** ppItem) = 0;
};
interface DAOTableDef : public _DAO {
  STDMETHOD(get_Attributes)(LONG* pAttributes) = 0;
  STDMETHOD(put_Attributes)(LONG Attributes) = 0;
  STDMETHOD(get_Connect)(BSTR* pConnect) = 0;
  STDMETHOD(put_Connect)(BSTR Connect) = 0;
  STDMETHOD(get_DateCreated)(VARIANT* pDateCreated) = 0;
  STDMETHOD(get_LastUpdated)(VARIANT* pLastUpdated) = 0;
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_SourceTableName)(BSTR* pSourceTableName) = 0;
  STDMETHOD(put_SourceTableName)(BSTR SourceTableName) = 0;
  STDMETHOD(get_Updatable)(VARIANT_BOOL* pUpdatable) = 0;
  STDMETHOD(get_ValidationText)(BSTR* pValidationText) = 0;
  STDMETHOD(put_ValidationText)(BSTR ValidationText) = 0;
  STDMETHOD(get_ValidationRule)(BSTR* pValidationRule) = 0;
  STDMETHOD(put_ValidationRule)(BSTR ValidationRule) = 0;
  STDMETHOD(get_RecordCount)(LONG* pRecordCount) = 0;
  STDMETHOD(get_Fields)(DAOFields** ppFields) = 0;
  STDMETHOD(get_Indexes)(DAOIndexes** ppIndexes) = 0;
  STDMETHOD(OpenRecordset)(VARIANT Type, VARIANT Options, DAORecordset** ppRecordset) = 0;
  STDMETHOD(RefreshLink)() = 0;
  STDMETHOD(CreateField)(VARIANT Name, VARIANT Type, VARIANT Size, DAOField** ppField) = 0;
  STDMETHOD(CreateIndex)(VARIANT Name, DAOIndex** ppIndex) = 0;
  STDMETHOD(CreateProperty)(VARIANT Name, VARIANT Type, VARIANT Value, VARIANT DDL, DAOProperty** ppProperty) = 0;
};
interface DAOTableDefs : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOTableDef** ppItem) = 0;
};
interface DAORelation : public _DAO {
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_Table)(BSTR* pTable) = 0;
  STDMETHOD(put_Table)(BSTR Table) = 0;
  STDMETHOD(get_ForeignTable)(BSTR* pForeignTable) = 0;
  STDMETHOD(put_ForeignTable)(BSTR ForeignTable) = 0;
  STDMETHOD(get_Attributes)(LONG* pAttributes) = 0;
  STDMETHOD(put_Attributes)(LONG Attributes) = 0;
  STDMETHOD(get_Fields)(DAOFields** ppFields) = 0;
  STDMETHOD(CreateField)(VARIANT Name, VARIANT Type, VARIANT Size, DAOField** ppField) = 0;
};
interface DAORelations : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAORelation** ppItem) = 0;
};
interface DAODatabase : public _DAO {
  STDMETHOD(get_CollatingOrder)(LONG* pCollatingOrder) = 0;
  STDMETHOD(get_Connect)(BSTR* pConnect) = 0;
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(get_QueryTimeout)(short* pQueryTimeout) = 0;
  STDMETHOD(put_QueryTimeout)(short QueryTimeout) = 0;
  STDMETHOD(get_Transactions)(VARIANT_BOOL* pTransactions) = 0;
  STDMETHOD(get_Updatable)(VARIANT_BOOL* pUpdatable) = 0;
  STDMETHOD(get_Version)(BSTR* pVersion) = 0;
  STDMETHOD(get_RecordsAffected)(LONG* pRecordsAffected) = 0;
  STDMETHOD(get_TableDefs)(DAOTableDefs** ppTableDefs) = 0;
  STDMETHOD(get_QueryDefs)(DAOQueryDefs** ppQueryDefs) = 0;
  STDMETHOD(get_Relations)(DAORelations** ppRelations) = 0;
  STDMETHOD(get_Containers)(IDispatch** ppContainers) = 0;
  STDMETHOD(get_Recordsets)(IDispatch** ppRecordsets) = 0;
  STDMETHOD(Close)() = 0;
  STDMETHOD(Execute)(BSTR Query, VARIANT Options) = 0;
  STDMETHOD(CreateProperty)(VARIANT Name, VARIANT Type, VARIANT Value, VARIANT DDL, DAOProperty** ppProperty) = 0;
  STDMETHOD(CreateRelation)(VARIANT Name, VARIANT Table, VARIANT ForeignTable, VARIANT Attributes, DAORelation** ppRelation) = 0;
  STDMETHOD(CreateTableDef)(VARIANT Name, VARIANT Attributes, VARIANT SourceTablename, VARIANT Connect, DAOTableDef** ppTableDef) = 0;
  STDMETHOD(CreateQueryDef)(VARIANT Name, VARIANT SQLText, DAOQueryDef** ppQueryDef) = 0;
  STDMETHOD(NewPassword)(BSTR OldPassword, BSTR NewPassword) = 0;
  STDMETHOD(OpenRecordset)(BSTR Name, VARIANT Type, VARIANT Options, VARIANT LockEdit, DAORecordset** ppRecordset) = 0;
};
interface DAODatabases : public _DAOCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAODatabase** ppItem) = 0;
};
interface DAOWorkspace : public _DAO {
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR Name) = 0;
  STDMETHOD(get_UserName)(BSTR* pUserName) = 0;
  STDMETHOD(get_IsolateODBCTrans)(short* pIsolateODBCTrans) = 0;
  STDMETHOD(put_IsolateODBCTrans)(short IsolateODBCTrans) = 0;
  STDMETHOD(get_Databases)(DAODatabases** ppDatabases) = 0;
  STDMETHOD(get_Users)(IDispatch** ppUsers) = 0;
  STDMETHOD(get_Groups)(IDispatch** ppGroups) = 0;
  STDMETHOD(BeginTrans)() = 0;
  STDMETHOD(CommitTrans)(LONG Options) = 0;
  STDMETHOD(Close)() = 0;
  STDMETHOD(Rollback)() = 0;
  STDMETHOD(OpenDatabase)(BSTR Name, VARIANT Options, VARIANT ReadOnly, VARIANT Connect, DAODatabase** ppDb) = 0;
  STDMETHOD(CreateDatabase)(BSTR Name, BSTR Connect, VARIANT Option, DAODatabase** ppDb) = 0;
  STDMETHOD(get_Type)(LONG* pType) = 0;
  STDMETHOD(get_LoginTimeout)(LONG* pLoginTimeout) = 0;
  STDMETHOD(put_LoginTimeout)(LONG LoginTimeout) = 0;
};
interface DAOWorkspaces : public _DAODynaCollection {
  STDMETHOD(get_Item)(VARIANT Item, DAOWorkspace** ppItem) = 0;
};
interface DAODBEngine : public _DAO {
  STDMETHOD(get_Version)(BSTR* pVersion) = 0;
  STDMETHOD(get_IniPath)(BSTR* pIniPath) = 0;
  STDMETHOD(put_IniPath)(BSTR IniPath) = 0;
  STDMETHOD(put_DefaultUser)(BSTR DefaultUser) = 0;
  STDMETHOD(put_DefaultPassword)(BSTR DefaultPassword) = 0;
  STDMETHOD(get_LoginTimeout)(short* pLoginTimeout) = 0;
  STDMETHOD(put_LoginTimeout)(short LoginTimeout) = 0;
  STDMETHOD(get_Workspaces)(DAOWorkspaces** ppWorkspaces) = 0;
  STDMETHOD(get_Errors)(DAOErrors** ppErrors) = 0;
  STDMETHOD(Idle)(VARIANT Action) = 0;
  STDMETHOD(CompactDatabase)(BSTR SrcName, BSTR DstName, VARIANT DstLocale, VARIANT Options, VARIANT SrcLocale) = 0;
  STDMETHOD(RepairDatabase)(BSTR Name) = 0;
  STDMETHOD(RegisterDatabase)(BSTR Dsn, BSTR Driver, VARIANT_BOOL Silent, BSTR Attributes) = 0;
  STDMETHOD(OpenDatabase)(BSTR Name, VARIANT Options, VARIANT ReadOnly, VARIANT Connect, DAODatabase** ppDb) = 0;
  STDMETHOD(CreateDatabase)(BSTR Name, BSTR Locale, VARIANT Option, DAODatabase** ppDb) = 0;
  STDMETHOD(FreeLocks)() = 0;
  STDMETHOD(BeginTrans)() = 0;
  STDMETHOD(CommitTrans)(LONG Option) = 0;
  STDMETHOD(Rollback)() = 0;
  STDMETHOD(SetDefaultWorkspace)(BSTR Name, BSTR Password) = 0;
  STDMETHOD(get_SystemDB)(BSTR* pSystemDB) = 0;
  STDMETHOD(put_SystemDB)(BSTR SystemDB) = 0;
  STDMETHOD(CreateWorkspace)(BSTR Name, BSTR UserName, BSTR Password, VARIANT UseType, DAOWorkspace** ppWorkspace) = 0;
  STDMETHOD(get_DefaultType)(LONG* pDefaultType) = 0;
  STDMETHOD(put_DefaultType)(LONG DefaultType) = 0;
  STDMETHOD(SetOption)(LONG Option, VARIANT Value) = 0;
};

// Constants
#define dbOpenTable 1
#define dbOpenDynaset 2
#define dbOpenSnapshot 4
#define dbOpenForwardOnly 8
#define dbOpenDynamic 16
#define dbEditNone 0
#define dbEditInProgress 1
#define dbEditAdd 2
#define dbUpdateRegular 1
#define dbUpdateCurrentRecord 2
#define dbUpdateBatch 4
#define dbDenyWrite 0x1
#define dbDenyRead 0x2
#define dbReadOnly 0x4
#define dbAppendOnly 0x8
#define dbInconsistent 0x10
#define dbConsistent 0x20
#define dbSQLPassThrough 0x40
#define dbFailOnError 0x80
#define dbForwardOnly 0x100
#define dbSeeChanges 0x200
#define dbRunAsync 0x400
#define dbExecDirect 0x800
#define dbPessimistic 2
#define dbOptimistic 3
#define dbOptimisticValue 1
#define dbOptimisticBatch 5
#define dbFreeLocks 1
#define dbRefreshCache 8
#define dbUseODBC 1
#define dbUseJet 2
#define dbBoolean 1
#define dbByte 2
#define dbInteger 3
#define dbLong 4
#define dbCurrency 5
#define dbSingle 6
#define dbDouble 7
#define dbDate 8
#define dbBinary 9
#define dbText 10
#define dbLongBinary 11
#define dbMemo 12
#define dbGUID 15
#define dbBigInt 16
#define dbVarBinary 17
#define dbChar 18
#define dbNumeric 19
#define dbDecimal 20
#define dbFloat 21
#define dbTime 22
#define dbTimeStamp 23
#define dbFixedField 0x1
#define dbVariableField 0x2
#define dbAutoIncrField 0x10
#define dbUpdatableField 0x20
#define dbSystemField 0x2000
#define dbHyperlinkField 0x8000
#define dbDescending 0x1
#define dbAttachExclusive 0x10000
#define dbAttachSavePWD 0x20000
#define dbSystemObject 0x80000002
#define dbAttachedTable 0x40000000
#define dbAttachedODBC 0x20000000
#define dbHiddenObject 0x1
#define dbRelationUnique 0x1
#define dbRelationDontEnforce 0x2
#define dbRelationInherited 0x4
#define dbRelationUpdateCascade 0x100
#define dbRelationDeleteCascade 0x1000
#define dbRelationLeft 0x1000000
#define dbRelationRight 0x2000000
#define dbVersion10 1
#define dbEncrypt 2
#define dbDecrypt 4
#define dbVersion11 8
#define dbVersion20 16
#define dbVersion30 32
#define dbVersion40 64
#define dbQSelect 0
#define dbQAction 0xF0
#define dbQCrosstab 16
#define dbQDelete 32
#define dbQUpdate 48
#define dbQAppend 64
#define dbQMakeTable 80
#define dbQDDL 96
#define dbQSQLPassThrough 112
#define dbQSetOperation 128
#define dbQSPTBulk 144
#define dbQCompound 160
#define dbLangGeneral TEXT(";LANGID=0x0409;CP=1252;COUNTRY=0")
#define dbLangArabic TEXT(";LANGID=0x0401;CP=1256;COUNTRY=0")
#define dbLangCzech TEXT(";LANGID=0x0405;CP=1250;COUNTRY=0")
#define dbLangDutch TEXT(";LANGID=0x0413;CP=1252;COUNTRY=0")
#define dbLangGreek TEXT(";LANGID=0x0408;CP=1253;COUNTRY=0")
#define dbLangHebrew TEXT(";LANGID=0x040D;CP=1255;COUNTRY=0")
#define dbLangHungarian TEXT(";LANGID=0x040E;CP=1250;COUNTRY=0")
#define dbLangIcelandic TEXT(";LANGID=0x040F;CP=1252;COUNTRY=0")
#define dbLangNordic TEXT(";LANGID=0x041D;CP=1252;COUNTRY=0")
#define dbLangNorwDan TEXT(";LANGID=0x0414;CP=1252;COUNTRY=0")
#define dbLangPolish TEXT(";LANGID=0x0415;CP=1250;COUNTRY=0")
#define dbLangCyrillic TEXT(";LANGID=0x0419;CP=1251;COUNTRY=0")
#define dbLangSpanish TEXT(";LANGID=0x040A;CP=1252;COUNTRY=0")
#define dbLangSwedFin TEXT(";LANGID=0x040B;CP=1252;COUNTRY=0")
#define dbLangTurkish TEXT(";LANGID=0x041F;CP=1254;COUNTRY=0")
#define dbLangJapanese TEXT(";LANGID=0x0411;CP=932;COUNTRY=0")
#define dbLangChineseSimplified TEXT(";LANGID=0x0804;CP=936;COUNTRY=0")
#define dbLangChineseTraditional TEXT(";LANGID=0x0404;CP=950;COUNTRY=0")
#define dbLangKorean TEXT(";LANGID=0x0412;CP=949;COUNTRY=0")
#define dbLangThai TEXT(";LANGID=0x041E;CP=874;COUNTRY=0")
#define dbLangSlovenian TEXT(";LANGID=0x0424;CP=1250;COUNTRY=0")

#endif
