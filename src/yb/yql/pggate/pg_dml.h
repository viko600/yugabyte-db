//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_DML_H_
#define YB_YQL_PGGATE_PG_DML_H_

#include <boost/unordered_map.hpp>

#include "yb/yql/pggate/pg_doc_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_table.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DML
//--------------------------------------------------------------------------------------------------
class PgSelectIndex;

class PgDml : public PgStatement {
 public:
  virtual ~PgDml();

  // Append a target in SELECT or RETURNING.
  CHECKED_STATUS AppendTarget(PgExpr *target);

  // Append a filter condition.
  // Supported expression kind is serialized Postgres expression
  CHECKED_STATUS AppendQual(PgExpr *qual);

  // Append a column reference.
  // If any serialized Postgres expressions appended to other lists require explicit addition
  // of their column references. Those column references should have Postgres type information.
  // Other PgExpr kinds are automatically scanned and their column references are appended.
  CHECKED_STATUS AppendColumnRef(PgExpr *colref);

  // Prepare column for both ends.
  // - Prepare protobuf to communicate with DocDB.
  // - Prepare PgExpr to send data back to Postgres layer.
  Result<const PgColumn&> PrepareColumnForRead(int attr_num, PgsqlExpressionPB *target_pb);
  CHECKED_STATUS PrepareColumnForWrite(PgColumn *pg_col, PgsqlExpressionPB *assign_pb);

  // Bind a column with an expression.
  // - For a secondary-index-scan, this bind specify the value of the secondary key which is used to
  //   query a row.
  // - For a primary-index-scan, this bind specify the value of the keys of the table.
  CHECKED_STATUS BindColumn(int attnum, PgExpr *attr_value);

  // Bind the whole table.
  CHECKED_STATUS BindTable();

  // Assign an expression to a column.
  CHECKED_STATUS AssignColumn(int attnum, PgExpr *attr_value);

  // Process the secondary index request if it is nested within this statement.
  Result<bool> ProcessSecondaryIndexRequest(const PgExecParameters *exec_params);

  // Fetch a row and return it to Postgres layer.
  CHECKED_STATUS Fetch(int32_t natts,
                       uint64_t *values,
                       bool *isnulls,
                       PgSysColumns *syscols,
                       bool *has_data);

  // Returns TRUE if docdb replies with more data.
  Result<bool> FetchDataFromServer();

  // Returns TRUE if desired row is found.
  Result<bool> GetNextRow(PgTuple *pg_tuple);

  virtual void SetCatalogCacheVersion(uint64_t catalog_cache_version) = 0;

  // Get column info on whether the column 'attr_num' is a hash key, a range
  // key, or neither.
  Result<YBCPgColumnInfo> GetColumnInfo(int attr_num) const;

  bool has_aggregate_targets();

  bool has_doc_op() {
    return doc_op_ != nullptr;
  }

 protected:
  // Method members.
  // Constructor.
  PgDml(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id);
  PgDml(PgSession::ScopedRefPtr pg_session,
        const PgObjectId& table_id,
        const PgObjectId& index_id,
        const PgPrepareParameters *prepare_params);

  // Allocate protobuf for a SELECTed expression.
  virtual PgsqlExpressionPB *AllocTargetPB() = 0;

  // Allocate protobuf for a WHERE clause expression.
  // Subclasses use different protobuf message types for their requests, so they should
  // implement this method that knows how to add a PgsqlExpressionPB entry into their where_clauses
  // field.
  virtual PgsqlExpressionPB *AllocQualPB() = 0;

  // Allocate protobuf for expression whose value is bounded to a column.
  virtual PgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) = 0;

  // Allocate protobuf for expression whose value is assigned to a column (SET clause).
  virtual PgsqlExpressionPB *AllocColumnAssignPB(PgColumn *col) = 0;

  // Specify target of the query in protobuf request.
  CHECKED_STATUS AppendTargetPB(PgExpr *target);

  // Update bind values.
  CHECKED_STATUS UpdateBindPBs();

  // Update set values.
  CHECKED_STATUS UpdateAssignPBs();

  // Compatibility: set deprecated column_refs for legacy nodes
  // We are deprecating PgsqlColumnRefsPB protobuf since it does not allow to transfer Postgres
  // type information required to evaluate serialized Postgres expressions.
  // It is being replaced by list of PgsqlColRefPB entries, which is set by ColRefsToPB.
  // While there is are chance of cluster being upgraded from older version, we have to populate
  // both.
  void ColumnRefsToPB(PgsqlColumnRefsPB *column_refs);

  // Transfer columns information from target_.columns() to the request's col_refs list field.
  // Subclasses use different protobuf message types to make requests, so they must implement
  // the ClearColRefPBs and AllocColRefPB virtual methods to respectively remove all old col_refs
  // entries and allocate new entry in their requests.
  void ColRefsToPB();

  // Clear previously allocated PgsqlColRefPB entries from the protobuf request
  virtual void ClearColRefPBs() = 0;

  // Allocate a PgsqlColRefPB entriy in the protobuf request
  virtual PgsqlColRefPB *AllocColRefPB() = 0;

  // -----------------------------------------------------------------------------------------------
  // Data members that define the DML statement.

  // Table identifiers
  // - table_id_ identifies the table to read data from.
  // - index_id_ identifies the index to be used for scanning.
  //
  // Example for query on table_id_ using index_id_.
  //   SELECT FROM "table_id_"
  //     WHERE ybctid IN (SELECT base_ybctid FROM "index_id_" WHERE matched-index-binds)
  //
  // - Postgres will create PgSelect(table_id_) { nested PgSelectIndex (index_id_) }
  // - When bind functions are called, it bind user-values to columns in PgSelectIndex as these
  //   binds will be used to find base_ybctid from the IndexTable.
  // - When AddTargets() is called, the target is added to PgSlect as data will be reading from
  //   table_id_ using the found base_ybctid from index_id_.
  PgObjectId table_id_;
  PgObjectId index_id_;

  // Targets of statements (Output parameter).
  // - "target_desc_" is the table descriptor where data will be read from.
  // - "targets_" are either selected or returned expressions by DML statements.
  PgTable target_;
  std::vector<PgExpr*> targets_;

  // Qual is a where clause condition pushed to the DocDB to filter scanned rows
  // Qual supports PgExprs holding serialized Postgres expressions, and require the column
  // references used in these Quals to be explicitly added with AppendColumnRef()
  std::vector<PgExpr*> quals_;

  // bind_desc_ is the descriptor of the table whose key columns' values will be specified by the
  // the DML statement being executed.
  // - For primary key binding, "bind_desc_" is the descriptor of the main table as we don't have
  //   a separated primary-index table.
  // - For secondary key binding, "bind_desc_" is the descriptor of teh secondary index table.
  //   The bound values will be used to read base_ybctid which is then used to read actual data
  //   from the main table.
  PgTable bind_;

  // Prepare control parameters.
  PgPrepareParameters prepare_params_ = { .index_oid = kInvalidOid,
                                          .index_only_scan = false,
                                          .use_secondary_index = false,
                                          .querying_colocated_table = false };

  // -----------------------------------------------------------------------------------------------
  // Data members for nested query: This is used for an optimization in PgGate.
  //
  // - Each DML operation can be understood as
  //     Read / Write TABLE WHERE ybctid IN (SELECT ybctid from INDEX).
  // - In most cases, the Postgres layer processes the subquery "SELECT ybctid from INDEX".
  // - Under certain conditions, to optimize the performance, the PgGate layer might operate on
  //   the INDEX subquery itself.
  std::unique_ptr<PgSelectIndex> secondary_index_query_;

  // -----------------------------------------------------------------------------------------------
  // Data members for generated protobuf.
  // NOTE:
  // - Where clause processing data is not supported yet.
  // - Some protobuf structure are also set up in PgColumn class.

  // Column associated values (expressions) to be used by DML statements.
  // - When expression are constructed, we bind them with their associated protobuf.
  // - These expressions might not yet have values for place_holders or literals.
  // - During execution, the place_holder values are updated, and the statement protobuf need to
  //   be updated accordingly.
  //
  // * Bind values are used to identify the selected rows to be operated on.
  // * Set values are used to hold columns' new values in the selected rows.
  bool ybctid_bind_ = false;
  boost::unordered_map<PgsqlExpressionPB*, PgExpr*> expr_binds_;
  std::unordered_map<PgsqlExpressionPB*, PgExpr*> expr_assigns_;

  // Used for colocated TRUNCATE that doesn't bind any columns.
  bool bind_table_ = false;

  // DML Operator.
  PgDocOp::SharedPtr doc_op_;

  //------------------------------------------------------------------------------------------------
  // Data members for navigating the output / result-set from either seleted or returned targets.
  std::list<PgDocResult> rowsets_;
  int64_t current_row_order_ = 0;

  // Yugabyte has a few IN/OUT parameters of statement execution, "pg_exec_params_" is used to sent
  // OUT value back to postgres.
  const PgExecParameters *pg_exec_params_ = NULL;

  //------------------------------------------------------------------------------------------------
  // Hashed and range values/components used to compute the tuple id.
  //
  // These members are populated by the AddYBTupleIdColumn function and the tuple id is retrieved
  // using the GetYBTupleId function.
  //
  // These members are not used internally by the statement and are simply a utility for computing
  // the tuple id (ybctid).
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DML_H_
