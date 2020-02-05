#include "protobuf_utils.h"

using gandiva::ConditionPtr;
using gandiva::DataTypePtr;
using gandiva::ExpressionPtr;
using gandiva::ExpressionVector;
using gandiva::FieldPtr;
using gandiva::FieldVector;
using gandiva::NodePtr;
using gandiva::NodeVector;
using gandiva::SchemaPtr;
using gandiva::Status;
using gandiva::TreeExprBuilder;

using gandiva::ArrayDataVector;

// forward declarations
NodePtr ProtoTypeToNode(const types::TreeNode& node);

DataTypePtr ProtoTypeToTime32(const types::ExtGandivaType& ext_type) {
  switch (ext_type.timeunit()) {
    case types::SEC:
      return arrow::time32(arrow::TimeUnit::SECOND);
    case types::MILLISEC:
      return arrow::time32(arrow::TimeUnit::MILLI);
    default:
      std::cerr << "Unknown time unit: " << ext_type.timeunit() << " for time32\n";
      return nullptr;
  }
}

DataTypePtr ProtoTypeToTime64(const types::ExtGandivaType& ext_type) {
  switch (ext_type.timeunit()) {
    case types::MICROSEC:
      return arrow::time64(arrow::TimeUnit::MICRO);
    case types::NANOSEC:
      return arrow::time64(arrow::TimeUnit::NANO);
    default:
      std::cerr << "Unknown time unit: " << ext_type.timeunit() << " for time64\n";
      return nullptr;
  }
}

DataTypePtr ProtoTypeToTimestamp(const types::ExtGandivaType& ext_type) {
  switch (ext_type.timeunit()) {
    case types::SEC:
      return arrow::timestamp(arrow::TimeUnit::SECOND);
    case types::MILLISEC:
      return arrow::timestamp(arrow::TimeUnit::MILLI);
    case types::MICROSEC:
      return arrow::timestamp(arrow::TimeUnit::MICRO);
    case types::NANOSEC:
      return arrow::timestamp(arrow::TimeUnit::NANO);
    default:
      std::cerr << "Unknown time unit: " << ext_type.timeunit() << " for timestamp\n";
      return nullptr;
  }
}

DataTypePtr ProtoTypeToDataType(const types::ExtGandivaType& ext_type) {
  switch (ext_type.type()) {
    case types::NONE:
      return arrow::null();
    case types::BOOL:
      return arrow::boolean();
    case types::UINT8:
      return arrow::uint8();
    case types::INT8:
      return arrow::int8();
    case types::UINT16:
      return arrow::uint16();
    case types::INT16:
      return arrow::int16();
    case types::UINT32:
      return arrow::uint32();
    case types::INT32:
      return arrow::int32();
    case types::UINT64:
      return arrow::uint64();
    case types::INT64:
      return arrow::int64();
    case types::HALF_FLOAT:
      return arrow::float16();
    case types::FLOAT:
      return arrow::float32();
    case types::DOUBLE:
      return arrow::float64();
    case types::UTF8:
      return arrow::utf8();
    case types::BINARY:
      return arrow::binary();
    case types::DATE32:
      return arrow::date32();
    case types::DATE64:
      return arrow::date64();
    case types::DECIMAL:
      // TODO: error handling
      return arrow::decimal(ext_type.precision(), ext_type.scale());
    case types::TIME32:
      return ProtoTypeToTime32(ext_type);
    case types::TIME64:
      return ProtoTypeToTime64(ext_type);
    case types::TIMESTAMP:
      return ProtoTypeToTimestamp(ext_type);

    case types::FIXED_SIZE_BINARY:
    case types::INTERVAL:
    case types::LIST:
    case types::STRUCT:
    case types::UNION:
    case types::DICTIONARY:
    case types::MAP:
      std::cerr << "Unhandled data type: " << ext_type.type() << "\n";
      return nullptr;

    default:
      std::cerr << "Unknown data type: " << ext_type.type() << "\n";
      return nullptr;
  }
}

FieldPtr ProtoTypeToField(const types::Field& f) {
  const std::string& name = f.name();
  DataTypePtr type = ProtoTypeToDataType(f.type());
  bool nullable = true;
  if (f.has_nullable()) {
    nullable = f.nullable();
  }

  return field(name, type, nullable);
}

NodePtr ProtoTypeToFieldNode(const types::FieldNode& node) {
  FieldPtr field_ptr = ProtoTypeToField(node.field());
  if (field_ptr == nullptr) {
    std::cerr << "Unable to create field node from protobuf\n";
    return nullptr;
  }

  return TreeExprBuilder::MakeField(field_ptr);
}

NodePtr ProtoTypeToFnNode(const types::FunctionNode& node) {
  const std::string& name = node.functionname();
  NodeVector children;

  for (int i = 0; i < node.inargs_size(); i++) {
    const types::TreeNode& arg = node.inargs(i);

    NodePtr n = ProtoTypeToNode(arg);
    if (n == nullptr) {
      std::cerr << "Unable to create argument for function: " << name << "\n";
      return nullptr;
    }

    children.push_back(n);
  }

  DataTypePtr return_type = ProtoTypeToDataType(node.returntype());
  if (return_type == nullptr) {
    std::cerr << "Unknown return type for function: " << name << "\n";
    return nullptr;
  }

  return TreeExprBuilder::MakeFunction(name, children, return_type);
}

NodePtr ProtoTypeToIfNode(const types::IfNode& node) {
  NodePtr cond = ProtoTypeToNode(node.cond());
  if (cond == nullptr) {
    std::cerr << "Unable to create cond node for if node\n";
    return nullptr;
  }

  NodePtr then_node = ProtoTypeToNode(node.thennode());
  if (then_node == nullptr) {
    std::cerr << "Unable to create then node for if node\n";
    return nullptr;
  }

  NodePtr else_node = ProtoTypeToNode(node.elsenode());
  if (else_node == nullptr) {
    std::cerr << "Unable to create else node for if node\n";
    return nullptr;
  }

  DataTypePtr return_type = ProtoTypeToDataType(node.returntype());
  if (return_type == nullptr) {
    std::cerr << "Unknown return type for if node\n";
    return nullptr;
  }

  return TreeExprBuilder::MakeIf(cond, then_node, else_node, return_type);
}

NodePtr ProtoTypeToAndNode(const types::AndNode& node) {
  NodeVector children;

  for (int i = 0; i < node.args_size(); i++) {
    const types::TreeNode& arg = node.args(i);

    NodePtr n = ProtoTypeToNode(arg);
    if (n == nullptr) {
      std::cerr << "Unable to create argument for boolean and\n";
      return nullptr;
    }
    children.push_back(n);
  }
  return TreeExprBuilder::MakeAnd(children);
}

NodePtr ProtoTypeToOrNode(const types::OrNode& node) {
  NodeVector children;

  for (int i = 0; i < node.args_size(); i++) {
    const types::TreeNode& arg = node.args(i);

    NodePtr n = ProtoTypeToNode(arg);
    if (n == nullptr) {
      std::cerr << "Unable to create argument for boolean or\n";
      return nullptr;
    }
    children.push_back(n);
  }
  return TreeExprBuilder::MakeOr(children);
}

NodePtr ProtoTypeToInNode(const types::InNode& node) {
  NodePtr field = ProtoTypeToFieldNode(node.field());

  if (node.has_intvalues()) {
    std::unordered_set<int32_t> int_values;
    for (int i = 0; i < node.intvalues().intvalues_size(); i++) {
      int_values.insert(node.intvalues().intvalues(i).value());
    }
    return TreeExprBuilder::MakeInExpressionInt32(field, int_values);
  }

  if (node.has_longvalues()) {
    std::unordered_set<int64_t> long_values;
    for (int i = 0; i < node.longvalues().longvalues_size(); i++) {
      long_values.insert(node.longvalues().longvalues(i).value());
    }
    return TreeExprBuilder::MakeInExpressionInt64(field, long_values);
  }

  if (node.has_stringvalues()) {
    std::unordered_set<std::string> stringvalues;
    for (int i = 0; i < node.stringvalues().stringvalues_size(); i++) {
      stringvalues.insert(node.stringvalues().stringvalues(i).value());
    }
    return TreeExprBuilder::MakeInExpressionString(field, stringvalues);
  }

  if (node.has_binaryvalues()) {
    std::unordered_set<std::string> stringvalues;
    for (int i = 0; i < node.binaryvalues().binaryvalues_size(); i++) {
      stringvalues.insert(node.binaryvalues().binaryvalues(i).value());
    }
    return TreeExprBuilder::MakeInExpressionBinary(field, stringvalues);
  }
  // not supported yet.
  std::cerr << "Unknown constant type for in expression.\n";
  return nullptr;
}

NodePtr ProtoTypeToNullNode(const types::NullNode& node) {
  DataTypePtr data_type = ProtoTypeToDataType(node.type());
  if (data_type == nullptr) {
    std::cerr << "Unknown type " << data_type->ToString() << " for null node\n";
    return nullptr;
  }

  return TreeExprBuilder::MakeNull(data_type);
}

NodePtr ProtoTypeToNode(const types::TreeNode& node) {
  if (node.has_fieldnode()) {
    return ProtoTypeToFieldNode(node.fieldnode());
  }

  if (node.has_fnnode()) {
    return ProtoTypeToFnNode(node.fnnode());
  }

  if (node.has_ifnode()) {
    return ProtoTypeToIfNode(node.ifnode());
  }

  if (node.has_andnode()) {
    return ProtoTypeToAndNode(node.andnode());
  }

  if (node.has_ornode()) {
    return ProtoTypeToOrNode(node.ornode());
  }

  if (node.has_innode()) {
    return ProtoTypeToInNode(node.innode());
  }

  if (node.has_nullnode()) {
    return ProtoTypeToNullNode(node.nullnode());
  }

  if (node.has_intnode()) {
    return TreeExprBuilder::MakeLiteral(node.intnode().value());
  }

  if (node.has_floatnode()) {
    return TreeExprBuilder::MakeLiteral(node.floatnode().value());
  }

  if (node.has_longnode()) {
    return TreeExprBuilder::MakeLiteral(node.longnode().value());
  }

  if (node.has_booleannode()) {
    return TreeExprBuilder::MakeLiteral(node.booleannode().value());
  }

  if (node.has_doublenode()) {
    return TreeExprBuilder::MakeLiteral(node.doublenode().value());
  }

  if (node.has_stringnode()) {
    return TreeExprBuilder::MakeStringLiteral(node.stringnode().value());
  }

  if (node.has_binarynode()) {
    return TreeExprBuilder::MakeBinaryLiteral(node.binarynode().value());
  }

  if (node.has_decimalnode()) {
    std::string value = node.decimalnode().value();
    gandiva::DecimalScalar128 literal(value, node.decimalnode().precision(),
                                      node.decimalnode().scale());
    return TreeExprBuilder::MakeDecimalLiteral(literal);
  }
  std::cerr << "Unknown node type in protobuf\n";
  return nullptr;
}

ExpressionPtr ProtoTypeToExpression(const types::ExpressionRoot& root) {
  NodePtr root_node = ProtoTypeToNode(root.root());
  if (root_node == nullptr) {
    std::cerr << "Unable to create expression node from expression protobuf\n";
    return nullptr;
  }

  FieldPtr field = ProtoTypeToField(root.resulttype());
  if (field == nullptr) {
    std::cerr << "Unable to extra return field from expression protobuf\n";
    return nullptr;
  }

  return TreeExprBuilder::MakeExpression(root_node, field);
}

ConditionPtr ProtoTypeToCondition(const types::Condition& condition) {
  NodePtr root_node = ProtoTypeToNode(condition.root());
  if (root_node == nullptr) {
    return nullptr;
  }

  return TreeExprBuilder::MakeCondition(root_node);
}

SchemaPtr ProtoTypeToSchema(const types::Schema& schema) {
  std::vector<FieldPtr> fields;

  for (int i = 0; i < schema.columns_size(); i++) {
    FieldPtr field = ProtoTypeToField(schema.columns(i));
    if (field == nullptr) {
      std::cerr << "Unable to extract arrow field from schema\n";
      return nullptr;
    }

    fields.push_back(field);
  }

  return arrow::schema(fields);
}

// Common for both projector and filters.

bool ParseProtobuf(uint8_t* buf, int bufLen, google::protobuf::Message* msg) {
  google::protobuf::io::CodedInputStream cis(buf, bufLen);
  cis.SetRecursionLimit(1000);
  return msg->ParseFromCodedStream(&cis);
}
