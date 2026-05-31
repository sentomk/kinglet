#include "compiler/compiler.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace kinglet {

namespace {

} // namespace

CompileResult Compiler::compile(const ast::Program &program) {
  chunk_ = Chunk();
  locals_.clear();
  errors_.clear();
  warnings_.clear();
  used_.clear();
  opened_.clear();
  function_indices_.clear();
  struct_indices_.clear();
  enum_indices_.clear();

  // Built-in error enums. Type checker registers the same trio; compiler
  // mirrors them into chunk metadata so variant construction (Ok(v) / Err(e))
  // and pattern matching emit valid (type_idx, variant_idx) pairs.
  {
    EnumMeta cast_err{"CastError", {"Empty", "NotANumber", "Overflow"}, {0, 1, 1}};
    enum_indices_["CastError"] = chunk_.add_enum_meta(std::move(cast_err));
    EnumMeta int_result{"IntResult", {"Ok", "Err"}, {1, 1}};
    enum_indices_["IntResult"] = chunk_.add_enum_meta(std::move(int_result));
    EnumMeta float_result{"FloatResult", {"Ok", "Err"}, {1, 1}};
    enum_indices_["FloatResult"] = chunk_.add_enum_meta(std::move(float_result));
  }

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(declaration.get())) {
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
      }
    }
    if (const auto *import_decl = dynamic_cast<const ast::ImportDecl *>(declaration.get())) {
      process_import(*import_decl);
    }
  }

  // Pass 1: register all structs, enums, and functions
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(declaration.get())) {
      if (!struct_decl->type_params.empty()) {
        generic_struct_decls_[struct_decl->name] = struct_decl;
        continue;
      }
      StructMeta meta;
      meta.name = struct_decl->name;
      for (const auto &field : struct_decl->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[struct_decl->name] = idx;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(declaration.get())) {
      EnumMeta meta;
      meta.name = enum_decl->name;
      for (const auto &v : enum_decl->variants) {
        meta.variants.push_back(v.name);
        meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
      }
      int idx = chunk_.add_enum_meta(std::move(meta));
      enum_indices_[enum_decl->name] = idx;
    }
  }

  // Pass 1b: register impl methods as functions
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *trait_decl = dynamic_cast<const ast::TraitDecl *>(declaration.get())) {
      trait_registry_[trait_decl->name] = trait_decl;
    }
    if (const auto *impl_decl = dynamic_cast<const ast::ImplDecl *>(declaration.get())) {
      std::unordered_set<std::string> overridden;
      for (const auto &method : impl_decl->methods) {
        std::string mangled = impl_decl->target_type + "::" + method->name;
        overridden.insert(method->name);
        int idx = chunk_.add_function(FunctionInfo{
            .name = mangled,
            .entry = 0,
            .param_count = static_cast<int>(method->params.size()),
        });
        uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
        function_indices_[mangled] = static_cast<int>(const_idx);
        method_return_types_[mangled] = method->return_type.name;
      }
      if (!impl_decl->trait_name.empty()) {
        auto trait_it = trait_registry_.find(impl_decl->trait_name);
        if (trait_it != trait_registry_.end()) {
          for (const auto &trait_method : trait_it->second->methods) {
            if (overridden.count(trait_method.name) == 0 && trait_method.default_body) {
              std::string mangled = impl_decl->target_type + "::" + trait_method.name;
              int param_count = static_cast<int>(trait_method.params.size());
              int idx = chunk_.add_function(FunctionInfo{
                  .name = mangled,
                  .entry = 0,
                  .param_count = param_count,
              });
              uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
              function_indices_[mangled] = static_cast<int>(const_idx);
              method_return_types_[mangled] = trait_method.return_type.name;
            }
          }
        }
      }
    }
  }

  std::vector<const ast::FunctionDecl *> functions;
  int main_index = -1;
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *function = dynamic_cast<const ast::FunctionDecl *>(declaration.get())) {
      if (!function->type_params.empty()) {
        generic_func_decls_[function->name] = function;
        continue;
      }
      int idx = chunk_.add_function(FunctionInfo{
          .name = function->name,
          .entry = 0,
          .param_count = static_cast<int>(function->params.size()),
      });
      uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
      function_indices_[function->name] = static_cast<int>(const_idx);
      method_return_types_[function->name] = function->return_type.name;
      functions.push_back(function);
      if (function->name == "main") {
        main_index = idx;
      }
    }
  }

  if (main_index < 0) {
    error_at(program.location, "Expected a main function.");
    return CompileResult{.chunk = std::move(chunk_), .errors = std::move(errors_), .warnings = std::move(warnings_)};
  }

  // Emit preamble: call main, then return its result
  ast::SourceLocation preamble_loc{1, 0};
  emit_constant(Value::function_value(main_index), preamble_loc);
  emit_operand(OpCode::Call, 0, preamble_loc);
  emit(OpCode::Return, preamble_loc);

  // Pass 2: compile each function body
  for (const auto *function : functions) {
    compile_function(*function);
    if (!errors_.empty()) break;
  }

  // Pass 2a: compile impl method bodies
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *impl_decl = dynamic_cast<const ast::ImplDecl *>(declaration.get())) {
      std::unordered_set<std::string> overridden;
      for (const auto &method : impl_decl->methods) {
        overridden.insert(method->name);
        std::string mangled = impl_decl->target_type + "::" + method->name;
        compile_function(*method, mangled);
        if (!errors_.empty()) break;
      }
      if (!errors_.empty()) break;
      if (!impl_decl->trait_name.empty()) {
        auto trait_it = trait_registry_.find(impl_decl->trait_name);
        if (trait_it != trait_registry_.end()) {
          for (const auto &trait_method : trait_it->second->methods) {
            if (overridden.count(trait_method.name) == 0 && trait_method.default_body) {
              std::string mangled = impl_decl->target_type + "::" + trait_method.name;
              locals_.clear();
              scope_stack_.clear();
              local_types_.clear();
              auto fit = function_indices_.find(mangled);
              int const_idx = fit->second;
              int func_idx = chunk_.constants()[static_cast<std::size_t>(const_idx)].function_index_storage;
              const_cast<FunctionInfo &>(chunk_.functions()[static_cast<std::size_t>(func_idx)]).entry =
                  chunk_.instructions().size();
              for (const auto &param : trait_method.params) {
                locals_.push_back(Local{.name = param.name, .is_mutable = true});
                if (param.name == "self") {
                  local_types_["self"] = impl_decl->target_type;
                } else {
                  std::string type_name = param.type.name;
                  if (type_name == "Self") type_name = impl_decl->target_type;
                  if (!type_name.empty()) local_types_[param.name] = type_name;
                }
              }
              implicit_return_stmt_ = nullptr;
              const auto *body = dynamic_cast<const ast::BlockStmt *>(trait_method.default_body.get());
              if (body && !body->statements.empty()) {
                const auto *last = dynamic_cast<const ast::ExprStmt *>(body->statements.back().get());
                if (last && trait_method.return_type.name != "void") {
                  implicit_return_stmt_ = last;
                }
              }
              compile_stmt(*trait_method.default_body);
              implicit_return_stmt_ = nullptr;
              if (errors_.empty()) {
                emit(OpCode::Null, impl_decl->location);
                emit(OpCode::Return, impl_decl->location);
              }
              if (!errors_.empty()) break;
            }
          }
        }
      }
    }
    if (!errors_.empty()) break;
  }

  // Pass 2b: compile imported function bodies
  for (const auto &[ns, func_list] : imported_function_decls_) {
    for (const auto *function : func_list) {
      std::string qualified = ns + "::" + function->name;
      compile_function(*function, qualified);
      if (!errors_.empty()) break;
    }
    if (!errors_.empty()) break;
  }

  // Pass 3: compile deferred generic function instantiations
  while (!pending_generic_funcs_.empty() && errors_.empty()) {
    auto pending = std::move(pending_generic_funcs_);
    pending_generic_funcs_.clear();
    for (const auto &[name, decl] : pending) {
      compile_function(*decl, name);
      if (!errors_.empty()) break;
    }
  }

  return CompileResult{.chunk = std::move(chunk_), .errors = std::move(errors_), .warnings = std::move(warnings_)};
}

CompileResult Compiler::compile_module(const ast::Program &program) {
  chunk_ = Chunk();
  locals_.clear();
  errors_.clear();
  warnings_.clear();
  used_.clear();
  opened_.clear();
  function_indices_.clear();
  struct_indices_.clear();
  enum_indices_.clear();

  // Built-in error enums. Type checker registers the same trio; compiler
  // mirrors them into chunk metadata so variant construction (Ok(v) / Err(e))
  // and pattern matching emit valid (type_idx, variant_idx) pairs.
  {
    EnumMeta cast_err{"CastError", {"Empty", "NotANumber", "Overflow"}, {0, 1, 1}};
    enum_indices_["CastError"] = chunk_.add_enum_meta(std::move(cast_err));
    EnumMeta int_result{"IntResult", {"Ok", "Err"}, {1, 1}};
    enum_indices_["IntResult"] = chunk_.add_enum_meta(std::move(int_result));
    EnumMeta float_result{"FloatResult", {"Ok", "Err"}, {1, 1}};
    enum_indices_["FloatResult"] = chunk_.add_enum_meta(std::move(float_result));
  }

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(declaration.get())) {
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
      }
    }
  }

  // Register structs and enums
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(declaration.get())) {
      if (!struct_decl->type_params.empty()) {
        generic_struct_decls_[struct_decl->name] = struct_decl;
        continue;
      }
      StructMeta meta;
      meta.name = struct_decl->name;
      for (const auto &field : struct_decl->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[struct_decl->name] = idx;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(declaration.get())) {
      EnumMeta meta;
      meta.name = enum_decl->name;
      for (const auto &v : enum_decl->variants) {
        meta.variants.push_back(v.name);
        meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
      }
      int idx = chunk_.add_enum_meta(std::move(meta));
      enum_indices_[enum_decl->name] = idx;
    }
  }

  // Register and compile all functions (no main required)
  std::vector<const ast::FunctionDecl *> functions;
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *function = dynamic_cast<const ast::FunctionDecl *>(declaration.get())) {
      if (!function->type_params.empty()) {
        generic_func_decls_[function->name] = function;
        continue;
      }
      int idx = chunk_.add_function(FunctionInfo{
          .name = function->name,
          .entry = 0,
          .param_count = static_cast<int>(function->params.size()),
      });
      uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
      function_indices_[function->name] = static_cast<int>(const_idx);
      functions.push_back(function);
    }
  }

  for (const auto *function : functions) {
    compile_function(*function);
    if (!errors_.empty()) break;
  }

  return CompileResult{.chunk = std::move(chunk_), .errors = std::move(errors_), .warnings = std::move(warnings_)};
}

void Compiler::push_scope() {
  scope_stack_.push_back(locals_.size());
}

void Compiler::pop_scope() {
  if (scope_stack_.empty()) return;
  const std::size_t target = scope_stack_.back();
  scope_stack_.pop_back();
  while (locals_.size() > target) {
    locals_.pop_back();
  }
}

std::string Compiler::infer_struct_type(const ast::Expr &expr) const {
  if (const auto *id = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    auto it = local_types_.find(id->name);
    if (it != local_types_.end()) return it->second;
    return "";
  }
  if (const auto *field = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    std::string parent_type = infer_struct_type(*field->object);
    if (parent_type.empty()) return "";
    auto si = struct_indices_.find(parent_type);
    if (si == struct_indices_.end()) return "";
    const auto &meta = chunk_.struct_metas()[static_cast<std::size_t>(si->second)];
    for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
      if (meta.field_names[i] == field->field_name) {
        return "";
      }
    }
    std::string method_key = parent_type + "::" + field->field_name;
    auto fi = function_indices_.find(method_key);
    if (fi != function_indices_.end()) return parent_type;
    return "";
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expr)) {
    if (const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call->callee.get())) {
      if (struct_indices_.count(callee_id->name)) return callee_id->name;
      auto ret_it = method_return_types_.find(callee_id->name);
      if (ret_it != method_return_types_.end() && struct_indices_.count(ret_it->second))
        return ret_it->second;
    }
    if (const auto *field_callee = dynamic_cast<const ast::FieldAccessExpr *>(call->callee.get())) {
      std::string obj_type = infer_struct_type(*field_callee->object);
      if (!obj_type.empty()) {
        std::string method_key = obj_type + "::" + field_callee->field_name;
        auto ret_it = method_return_types_.find(method_key);
        if (ret_it != method_return_types_.end() && struct_indices_.count(ret_it->second))
          return ret_it->second;
      }
    }
    return "";
  }
  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    return struct_lit->struct_type.name;
  }
  return "";
}

void Compiler::compile_function(const ast::FunctionDecl &function, const std::string &lookup_name) {
  locals_.clear();
  scope_stack_.clear();
  local_types_.clear();

  // Look up this function's index and patch its entry point
  const std::string &name = lookup_name.empty() ? function.name : lookup_name;
  auto it = function_indices_.find(name);
  int const_idx = it->second;
  int func_idx = chunk_.constants()[static_cast<std::size_t>(const_idx)].function_index_storage;
  const_cast<FunctionInfo &>(chunk_.functions()[static_cast<std::size_t>(func_idx)]).entry =
      chunk_.instructions().size();

  // Parameters become locals at slots 0..N-1
  for (const auto &param : function.params) {
    locals_.push_back(Local{.name = param.name, .is_mutable = true});
    if (param.name == "self" && !lookup_name.empty()) {
      auto sep = lookup_name.find("::");
      if (sep != std::string::npos) {
        local_types_["self"] = lookup_name.substr(0, sep);
      }
    } else if (!param.type.name.empty()) {
      local_types_[param.name] = param.type.name;
    }
  }

  // Detect implicit return: if last statement in body is an ExprStmt,
  // compile it as a return instead of discarding the value.
  const auto *body = dynamic_cast<const ast::BlockStmt *>(function.body.get());
  if (body && !body->statements.empty()) {
    const auto *last = dynamic_cast<const ast::ExprStmt *>(body->statements.back().get());
    if (last && function.return_type.name != "void") {
      implicit_return_stmt_ = last;
    }
  }

  compile_stmt(*function.body);
  implicit_return_stmt_ = nullptr;

  // Fallthrough safety: implicit null return
  if (errors_.empty()) {
    emit(OpCode::Null, function.location);
    emit(OpCode::Return, function.location);
  }
}

void Compiler::compile_stmt(const ast::Stmt &stmt) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    push_scope();
    bool returned = false;
    for (const ast::StmtPtr &statement : block->statements) {
      if (returned) break;
      compile_stmt(*statement);
      if (!errors_.empty()) {
        return;
      }
      if (dynamic_cast<const ast::ReturnStmt *>(statement.get())) {
        returned = true;
      }
    }
    pop_scope();
    return;
  }

  if (const auto *return_stmt = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (return_stmt->value) {
      compile_expr(*return_stmt->value);
    } else {
      emit(OpCode::Null, return_stmt->location);
    }
    emit(OpCode::Return, return_stmt->location);
    return;
  }

  if (const auto *var_decl = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    uint32_t slot = 0;
    if (!declare_local(*var_decl, &slot)) {
      return;
    }
    if (!var_decl->type.name.empty()) {
      local_types_[var_decl->name] = var_decl->type.name;
    } else if (var_decl->init) {
      if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(var_decl->init.get())) {
        local_types_[var_decl->name] = struct_lit->struct_type.name;
      }
    }
    if (var_decl->init) {
      compile_expr(*var_decl->init);
    } else {
      emit(OpCode::Null, var_decl->location);
    }
    emit_operand(OpCode::StoreLocal, slot, var_decl->location);
    emit(OpCode::Pop, var_decl->location);
    return;
  }

  if (const auto *unpack = dynamic_cast<const ast::UnpackDeclStmt *>(&stmt)) {
    compile_expr(*unpack->init);
    uint32_t arr_slot = static_cast<uint32_t>(locals_.size());
    locals_.push_back(Local{.name = "$unpack_tmp", .is_mutable = false});
    emit_operand(OpCode::StoreLocal, arr_slot, unpack->location);
    emit(OpCode::Pop, unpack->location);

    for (std::size_t i = 0; i < unpack->names.size(); ++i) {
      uint32_t slot = static_cast<uint32_t>(locals_.size());
      locals_.push_back(Local{.name = unpack->names[i], .is_mutable = true});
      emit_operand(OpCode::LoadLocal, arr_slot, unpack->location);
      emit_constant(Value::int_value(static_cast<int64_t>(i)), unpack->location);
      emit(OpCode::IndexGet, unpack->location);
      emit_operand(OpCode::StoreLocal, slot, unpack->location);
      emit(OpCode::Pop, unpack->location);
    }

    if (!unpack->rest_name.empty()) {
      uint32_t slot = static_cast<uint32_t>(locals_.size());
      locals_.push_back(Local{.name = unpack->rest_name, .is_mutable = true});
      emit_operand(OpCode::LoadLocal, arr_slot, unpack->location);
      emit_constant(Value::int_value(static_cast<int64_t>(unpack->names.size())), unpack->location);
      emit_operand(OpCode::LoadLocal, arr_slot, unpack->location);
      emit(OpCode::ArrayLen, unpack->location);
      emit(OpCode::ArraySlice, unpack->location);
      emit_operand(OpCode::StoreLocal, slot, unpack->location);
      emit(OpCode::Pop, unpack->location);
    }
    return;
  }

  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    compile_expr(*expr_stmt->expr);
    if (expr_stmt == implicit_return_stmt_) {
      emit(OpCode::Return, expr_stmt->location);
    } else {
      emit(OpCode::Pop, expr_stmt->location);
    }
    return;
  }

  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    compile_expr(*if_stmt->condition);
    const std::size_t then_jump = emit_jump(OpCode::JmpFalse, if_stmt->location);
    compile_stmt(*if_stmt->then_branch);
    if (if_stmt->else_branch) {
      const std::size_t else_jump = emit_jump(OpCode::Jmp, if_stmt->location);
      patch_jump(then_jump);
      compile_stmt(*if_stmt->else_branch);
      patch_jump(else_jump);
    } else {
      patch_jump(then_jump);
    }
    return;
  }

  if (const auto *guard_stmt = dynamic_cast<const ast::GuardStmt *>(&stmt)) {
    compile_expr(*guard_stmt->condition);
    const std::size_t else_jump = emit_jump(OpCode::JmpFalse, guard_stmt->location);
    const std::size_t skip_jump = emit_jump(OpCode::Jmp, guard_stmt->location);
    patch_jump(else_jump);
    compile_stmt(*guard_stmt->else_body);
    patch_jump(skip_jump);
    return;
  }

  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    loop_stack_.emplace_back();

    const std::size_t loop_start = chunk_.instructions().size();
    compile_expr(*while_stmt->condition);
    loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::JmpFalse, while_stmt->location));
    compile_stmt(*while_stmt->body);
    const std::size_t loop_jump = emit_jump(OpCode::Jmp, while_stmt->location);
    patch_jump_to(loop_jump, loop_start);

    for (std::size_t jump : loop_stack_.back().break_jumps) {
      patch_jump(jump);
    }
    for (std::size_t jump : loop_stack_.back().continue_jumps) {
      patch_jump_to(jump, loop_start);
    }
    loop_stack_.pop_back();
    return;
  }

  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    push_scope();
    loop_stack_.emplace_back();

    if (for_stmt->init) {
      compile_stmt(*for_stmt->init);
    }

    const std::size_t loop_start = chunk_.instructions().size();
    if (for_stmt->condition) {
      compile_expr(*for_stmt->condition);
      loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::JmpFalse, for_stmt->location));
    }
    compile_stmt(*for_stmt->body);

    const std::size_t step_start = chunk_.instructions().size();
    for (std::size_t jump : loop_stack_.back().continue_jumps) {
      patch_jump_to(jump, step_start);
    }

    if (for_stmt->step) {
      compile_stmt(*for_stmt->step);
    }

    const std::size_t loop_jump = emit_jump(OpCode::Jmp, for_stmt->location);
    patch_jump_to(loop_jump, loop_start);

    for (std::size_t jump : loop_stack_.back().break_jumps) {
      patch_jump(jump);
    }
    loop_stack_.pop_back();
    pop_scope();
    return;
  }

  if (dynamic_cast<const ast::BreakStmt *>(&stmt)) {
    if (loop_stack_.empty()) {
      error_at(stmt.location, "break must be inside a loop.");
      return;
    }
    const std::size_t jump = emit_jump(OpCode::Jmp, stmt.location);
    loop_stack_.back().break_jumps.push_back(jump);
    return;
  }

  if (dynamic_cast<const ast::ContinueStmt *>(&stmt)) {
    if (loop_stack_.empty()) {
      error_at(stmt.location, "continue must be inside a loop.");
      return;
    }
    const std::size_t jump = emit_jump(OpCode::Jmp, stmt.location);
    loop_stack_.back().continue_jumps.push_back(jump);
    return;
  }

  error_at(stmt.location, "Unsupported statement in VM compiler.");
}

void Compiler::compile_expr(const ast::Expr &expr) {
  if (const auto *int_lit = dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    emit_constant(Value::int_value(int_lit->value), int_lit->location);
    return;
  }

  if (const auto *float_lit = dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    emit_constant(Value::double_value(float_lit->value), float_lit->location);
    return;
  }

  if (const auto *string_lit = dynamic_cast<const ast::StringLiteralExpr *>(&expr)) {
    // Unescape: \n -> newline, \t -> tab, etc.
    std::string unescaped;
    unescaped.reserve(string_lit->value.size());
    for (std::size_t i = 0; i < string_lit->value.size(); ++i) {
      if (string_lit->value[i] == '\\' && i + 1 < string_lit->value.size()) {
        switch (string_lit->value[i + 1]) {
        case 'n':
          unescaped += '\n';
          ++i;
          break;
        case 't':
          unescaped += '\t';
          ++i;
          break;
        case 'r':
          unescaped += '\r';
          ++i;
          break;
        case '\\':
          unescaped += '\\';
          ++i;
          break;
        case '"':
          unescaped += '"';
          ++i;
          break;
        default:
          unescaped += string_lit->value[i];
          break;
        }
      } else {
        unescaped += string_lit->value[i];
      }
    }
    emit_constant(Value::string_value(unescaped), string_lit->location);
    return;
  }

  if (const auto *bool_lit = dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) {
    emit(bool_lit->value ? OpCode::True : OpCode::False, bool_lit->location);
    return;
  }

  if (dynamic_cast<const ast::NullLiteralExpr *>(&expr)) {
    emit(OpCode::Null, expr.location);
    return;
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    compile_expr(*unary->right);
    switch (unary->op) {
    case ast::UnaryOp::Neg:
      emit(OpCode::Negate, unary->location);
      break;
    case ast::UnaryOp::Not:
      emit(OpCode::Not, unary->location);
      break;
    case ast::UnaryOp::BitNot:
      emit(OpCode::BitNot, unary->location);
      break;
    default:
      error_at(unary->location, "Unsupported unary operator.");
      break;
    }
    return;
  }

  if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    const int slot = resolve_local(identifier->name);
    if (slot < 0) {
      error_at(identifier->location, "Use of undeclared variable '" + identifier->name + "'.");
      return;
    }
    emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(slot), identifier->location);
    return;
  }

  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    compile_assignment(*assign);
    return;
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    if (binary->op == ast::BinaryOp::And) {
      compile_expr(*binary->left);
      std::size_t false_jump = emit_jump(OpCode::JmpFalse, binary->location);
      compile_expr(*binary->right);
      std::size_t end_jump = emit_jump(OpCode::Jmp, binary->location);
      patch_jump(false_jump);
      emit(OpCode::False, binary->location);
      patch_jump(end_jump);
      return;
    }
    if (binary->op == ast::BinaryOp::Or) {
      compile_expr(*binary->left);
      std::size_t false_jump = emit_jump(OpCode::JmpFalse, binary->location);
      emit(OpCode::True, binary->location);
      std::size_t end_jump = emit_jump(OpCode::Jmp, binary->location);
      patch_jump(false_jump);
      compile_expr(*binary->right);
      patch_jump(end_jump);
      return;
    }
    compile_expr(*binary->left);
    compile_expr(*binary->right);
    switch (binary->op) {
    case ast::BinaryOp::Add:
      emit(OpCode::Add, binary->location);
      break;
    case ast::BinaryOp::Sub:
      emit(OpCode::Subtract, binary->location);
      break;
    case ast::BinaryOp::Mul:
      emit(OpCode::Multiply, binary->location);
      break;
    case ast::BinaryOp::Div:
      emit(OpCode::Divide, binary->location);
      break;
    case ast::BinaryOp::Mod:
      emit(OpCode::Modulo, binary->location);
      break;
    case ast::BinaryOp::Eq:
      emit(OpCode::Eq, binary->location);
      break;
    case ast::BinaryOp::Neq:
      emit(OpCode::Neq, binary->location);
      break;
    case ast::BinaryOp::Lt:
      emit(OpCode::Lt, binary->location);
      break;
    case ast::BinaryOp::Gt:
      emit(OpCode::Gt, binary->location);
      break;
    case ast::BinaryOp::Le:
      emit(OpCode::Le, binary->location);
      break;
    case ast::BinaryOp::Ge:
      emit(OpCode::Ge, binary->location);
      break;
    case ast::BinaryOp::BitAnd:
      emit(OpCode::BitAnd, binary->location);
      break;
    case ast::BinaryOp::BitOr:
      emit(OpCode::BitOr, binary->location);
      break;
    case ast::BinaryOp::BitXor:
      emit(OpCode::BitXor, binary->location);
      break;
    case ast::BinaryOp::Shl:
      emit(OpCode::Shl, binary->location);
      break;
    case ast::BinaryOp::Shr:
      emit(OpCode::Shr, binary->location);
      break;
    default:
      error_at(binary->location, "Unsupported binary operator.");
      break;
    }
    return;
  }

  if (const auto *call_expr = dynamic_cast<const ast::CallExpr *>(&expr)) {
    const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
    // Handle bare io:: members when 'using namespace io;' is in effect
    if (callee_id && opened_.count("io") != 0) {
      if (callee_id->name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeOut, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (callee_id->name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeErr, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (callee_id->name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeIn, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    const auto *ns_callee =
        dynamic_cast<const ast::NamespaceAccessExpr *>(call_expr->callee.get());
    if (ns_callee && used_.count(ns_callee->namespace_name) != 0 &&
        ns_callee->namespace_name == "io") {
      if (ns_callee->member_name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeOut, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }

      if (ns_callee->member_name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeErr, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }

      if (ns_callee->member_name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeIn, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // Handle fs::__read(...) / fs::__write(...) direct calls.
    if (ns_callee && used_.count("fs") != 0 && ns_callee->namespace_name == "fs") {
      if (ns_callee->member_name == "__read") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeFsRead, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (ns_callee->member_name == "__write") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeFsWrite, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // Handle sys::args() direct call.
    if (ns_callee && used_.count("sys") != 0 && ns_callee->namespace_name == "sys") {
      if (ns_callee->member_name == "args") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeSysArgs, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // Handle enum variant construction with payload: Shape::Circle(1.0)
    if (ns_callee) {
      auto enum_it = enum_indices_.find(ns_callee->namespace_name);
      if (enum_it != enum_indices_.end()) {
        int type_idx = enum_it->second;
        const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
        int variant_idx = -1;
        for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
          if (meta.variants[static_cast<std::size_t>(i)] == ns_callee->member_name) {
            variant_idx = i;
            break;
          }
        }
        if (variant_idx < 0) {
          error_at(ns_callee->location, "Unknown enum variant '" + ns_callee->member_name + "'.");
          return;
        }
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) |
                           static_cast<uint32_t>(variant_idx);
        emit_operand(OpCode::EnumVariantPayload, operand, call_expr->location);
        return;
      }
    }

    // Handle io::out.line(...), io::err.line(...), io::in.secret(...)
    const auto *field_callee =
        dynamic_cast<const ast::FieldAccessExpr *>(call_expr->callee.get());
    if (field_callee) {
      const auto *ns_obj =
          dynamic_cast<const ast::NamespaceAccessExpr *>(field_callee->object.get());
      if (ns_obj && ns_obj->namespace_name == "io" && used_.count("io") != 0) {
        if (ns_obj->member_name == "out" && field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeOutLn, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
        if (ns_obj->member_name == "err" && field_callee->field_name == "line") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeErrLn, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
        if (ns_obj->member_name == "in" && field_callee->field_name == "secret") {
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_operand(OpCode::NativeInSecret, static_cast<uint32_t>(call_expr->args.size()),
                       call_expr->location);
          return;
        }
      }
    }

    // Handle array/string method calls
    if (field_callee) {
      const std::string &method = field_callee->field_name;
      if (method == "len" || method == "push" || method == "pop" ||
          method == "remove" || method == "contains" || method == "clear" ||
          method == "insert" || method == "index_of" || method == "slice" ||
          method == "reverse" || method == "resize" || method == "has" ||
          method == "keys" || method == "starts_with" ||
          method == "ends_with" || method == "replace" || method == "split" ||
          method == "trim" || method == "to_upper" || method == "to_lower" ||
          method == "to_int" || method == "to_float" ||
          method == "code" || method == "code_at") {
        compile_expr(*field_callee->object);
        if (method == "len") {
          emit(OpCode::ArrayLen, call_expr->location);
          return;
        }
        if (method == "has") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::MapHas, call_expr->location);
          return;
        }
        if (method == "keys") {
          emit(OpCode::MapKeys, call_expr->location);
          return;
        }
        if (method == "push") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayPush, call_expr->location);
          return;
        }
        if (method == "resize") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::ArrayResize, call_expr->location);
          return;
        }
        if (method == "pop") {
          emit(OpCode::ArrayPop, call_expr->location);
          return;
        }
        if (method == "remove") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayRemove, call_expr->location);
          return;
        }
        if (method == "contains") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayContains, call_expr->location);
          return;
        }
        if (method == "clear") {
          emit(OpCode::ArrayClear, call_expr->location);
          return;
        }
        if (method == "insert") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::ArrayInsert, call_expr->location);
          return;
        }
        if (method == "index_of") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::ArrayIndexOf, call_expr->location);
          return;
        }
        if (method == "slice") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::ArraySlice, call_expr->location);
          return;
        }
        if (method == "reverse") {
          emit(OpCode::ArrayReverse, call_expr->location);
          return;
        }
        if (method == "starts_with") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringStartsWith, call_expr->location);
          return;
        }
        if (method == "ends_with") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringEndsWith, call_expr->location);
          return;
        }
        if (method == "replace") {
          compile_expr(*call_expr->args[0]);
          compile_expr(*call_expr->args[1]);
          emit(OpCode::StringReplace, call_expr->location);
          return;
        }
        if (method == "split") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringSplit, call_expr->location);
          return;
        }
        if (method == "trim") {
          emit(OpCode::StringTrim, call_expr->location);
          return;
        }
        if (method == "to_upper") {
          emit(OpCode::StringToUpper, call_expr->location);
          return;
        }
        if (method == "to_lower") {
          emit(OpCode::StringToLower, call_expr->location);
          return;
        }
        if (method == "to_int") {
          emit(OpCode::StringToInt, call_expr->location);
          return;
        }
        if (method == "to_float") {
          emit(OpCode::StringToFloat, call_expr->location);
          return;
        }
        if (method == "code") {
          emit(OpCode::StringCode, call_expr->location);
          return;
        }
        if (method == "code_at") {
          compile_expr(*call_expr->args[0]);
          emit(OpCode::StringCodeAt, call_expr->location);
          return;
        }
      }
    }

    // Handle impl method calls: obj.method(args...)
    if (field_callee) {
      std::string obj_type = infer_struct_type(*field_callee->object);
      if (!obj_type.empty()) {
        std::string method_key = obj_type + "::" + field_callee->field_name;
        auto func_it = function_indices_.find(method_key);
        if (func_it != function_indices_.end()) {
          compile_expr(*field_callee->object);
          for (const ast::ExprPtr &arg : call_expr->args) {
            compile_expr(*arg);
          }
          emit_constant(Value::function_value(
              chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_index_storage),
              call_expr->location);
          emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size() + 1),
                       call_expr->location);
          return;
        }
      }
    }

    // Generic function call
    if (callee_id && !call_expr->type_args.empty()) {
      std::string mangled = callee_id->name;
      for (const auto &arg : call_expr->type_args) {
        mangled += "__" + arg.to_string();
      }
      auto func_it = function_indices_.find(mangled);
      if (func_it == function_indices_.end()) {
        auto gen_it = generic_func_decls_.find(callee_id->name);
        if (gen_it != generic_func_decls_.end()) {
          const ast::FunctionDecl *decl = gen_it->second;
          int idx = chunk_.add_function(FunctionInfo{
              .name = mangled,
              .entry = 0,
              .param_count = static_cast<int>(decl->params.size()),
          });
          uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
          function_indices_[mangled] = static_cast<int>(const_idx);
          pending_generic_funcs_.push_back({mangled, decl});
        }
      }
      func_it = function_indices_.find(mangled);
      if (func_it != function_indices_.end()) {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_constant(Value::function_value(
            chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_index_storage),
            call_expr->location);
        emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
        return;
      }
    }

    // User-defined function call
    if (callee_id) {
      auto func_it = function_indices_.find(callee_id->name);
      if (func_it != function_indices_.end()) {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_constant(Value::function_value(
            chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_index_storage),
            call_expr->location);
        emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
        return;
      }
    }

    for (const ast::ExprPtr &arg : call_expr->args) {
      compile_expr(*arg);
    }
    compile_expr(*call_expr->callee);
    emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
    return;
  }

  if (const auto *match_expr = dynamic_cast<const ast::MatchExpr *>(&expr)) {
    compile_expr(*match_expr->value);
    const uint32_t temp_slot = static_cast<uint32_t>(locals_.size());
    locals_.push_back(Local{.name = "<match_value>", .is_mutable = false});
    emit_operand(OpCode::StoreLocal, temp_slot, match_expr->location);

    std::vector<std::size_t> end_jumps;
    for (const ast::MatchArm &arm : match_expr->arms) {
      const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(arm.pattern.get());
      const auto *binding = dynamic_cast<const ast::BindingPattern *>(arm.pattern.get());

      if (identifier && identifier->name == "_") {
        if (arm.guard) {
          compile_expr(*arm.guard);
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
        } else {
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
        }
      } else if (binding) {
        const uint32_t bind_slot = static_cast<uint32_t>(locals_.size());
        locals_.push_back(Local{.name = binding->name, .is_mutable = false});
        emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
        emit_operand(OpCode::StoreLocal, bind_slot, match_expr->location);
        emit(OpCode::Pop, match_expr->location);
        if (arm.guard) {
          compile_expr(*arm.guard);
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
        } else {
          compile_expr(*arm.body);
        }
        locals_.pop_back();
      } else if (const auto *arr_pat = dynamic_cast<const ast::ArrayPattern *>(arm.pattern.get())) {
        std::vector<uint32_t> bind_slots;
        std::vector<std::size_t> fail_jumps;
        for (std::size_t i = 0; i < arr_pat->elements.size(); ++i) {
          const auto &elem = arr_pat->elements[i];
          const auto *elem_binding = dynamic_cast<const ast::BindingPattern *>(elem.get());
          const auto *elem_wildcard = dynamic_cast<const ast::IdentifierExpr *>(elem.get());
          if (elem_binding) {
            const uint32_t slot = static_cast<uint32_t>(locals_.size());
            locals_.push_back(Local{.name = elem_binding->name, .is_mutable = false});
            emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
            emit_constant(Value::int_value(static_cast<int>(i)), match_expr->location);
            emit(OpCode::IndexGet, match_expr->location);
            emit_operand(OpCode::StoreLocal, slot, match_expr->location);
            emit(OpCode::Pop, match_expr->location);
            bind_slots.push_back(slot);
          } else if (elem_wildcard && elem_wildcard->name == "_") {
            // wildcard element, skip
          } else {
            emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
            emit_constant(Value::int_value(static_cast<int>(i)), match_expr->location);
            emit(OpCode::IndexGet, match_expr->location);
            compile_expr(*elem);
            emit(OpCode::Eq, match_expr->location);
            fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          }
        }
        if (arm.guard) {
          compile_expr(*arm.guard);
          fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          emit(OpCode::Pop, match_expr->location);
        }
        compile_expr(*arm.body);
        end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
        for (std::size_t fj : fail_jumps) {
          patch_jump(fj);
        }
        for (std::size_t j = 0; j < bind_slots.size(); ++j) {
          locals_.pop_back();
        }
      } else if (const auto *enum_pat = dynamic_cast<const ast::EnumPattern *>(arm.pattern.get())) {
        // Enum pattern: match variant and optionally extract payload
        auto enum_it = enum_indices_.find(enum_pat->enum_name);
        if (enum_it == enum_indices_.end()) {
          error_at(enum_pat->location, "Unknown enum type '" + enum_pat->enum_name + "'.");
          return;
        }
        int type_idx = enum_it->second;
        const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
        int variant_idx = -1;
        for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
          if (meta.variants[static_cast<std::size_t>(i)] == enum_pat->variant_name) {
            variant_idx = i;
            break;
          }
        }
        if (variant_idx < 0) {
          error_at(enum_pat->location, "Unknown enum variant '" + enum_pat->variant_name + "'.");
          return;
        }

        std::vector<std::size_t> fail_jumps;
        std::vector<uint32_t> bind_slots;

        // Check if value matches this enum type and variant
        // Stack: [initial_val]
        emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
        uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) | static_cast<uint32_t>(variant_idx);
        emit_operand(OpCode::EnumVariant, operand, enum_pat->location);
        emit(OpCode::Eq, enum_pat->location);
        fail_jumps.push_back(emit_jump(OpCode::JmpFalse, enum_pat->location));
        // JmpFalse popped the Eq result; initial_val still on stack.

        // Extract payload bindings — these push/pop around initial_val.
        for (std::size_t i = 0; i < enum_pat->fields.size(); ++i) {
          const auto *field_binding = dynamic_cast<const ast::BindingPattern *>(enum_pat->fields[i].get());
          if (field_binding) {
            const uint32_t slot = static_cast<uint32_t>(locals_.size());
            locals_.push_back(Local{.name = field_binding->name, .is_mutable = false});
            emit_operand(OpCode::LoadLocal, temp_slot, enum_pat->location);
            emit_operand(OpCode::EnumPayloadGet, static_cast<uint32_t>(i), enum_pat->location);
            emit_operand(OpCode::StoreLocal, slot, enum_pat->location);
            emit(OpCode::Pop, enum_pat->location);
            bind_slots.push_back(slot);
          }
        }

        if (arm.guard) {
          compile_expr(*arm.guard);
          fail_jumps.push_back(emit_jump(OpCode::JmpFalse, match_expr->location));
          // Pop initial_val so body result is clean on stack.
          emit(OpCode::Pop, match_expr->location);
        } else {
          // No guard: pop initial_val before body.
          emit(OpCode::Pop, match_expr->location);
        }

        compile_expr(*arm.body);
        end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));

        for (std::size_t fj : fail_jumps) {
          patch_jump(fj);
        }
        for (std::size_t j = 0; j < bind_slots.size(); ++j) {
          locals_.pop_back();
        }
      } else {
        emit_operand(OpCode::LoadLocal, temp_slot, match_expr->location);
        compile_expr(*arm.pattern);
        emit(OpCode::Eq, match_expr->location);
        if (arm.guard) {
          const std::size_t skip_guard = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.guard);
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
          patch_jump(skip_guard);
        } else {
          const std::size_t next_arm = emit_jump(OpCode::JmpFalse, match_expr->location);
          emit(OpCode::Pop, match_expr->location);
          compile_expr(*arm.body);
          end_jumps.push_back(emit_jump(OpCode::Jmp, match_expr->location));
          patch_jump(next_arm);
        }
      }
    }

    for (std::size_t jump : end_jumps) {
      patch_jump(jump);
    }

    locals_.pop_back();
    return;
  }

  if (const auto *cast_expr = dynamic_cast<const ast::CastExpr *>(&expr)) {
    compile_expr(*cast_expr->value);
    uint32_t target_kind = 0;
    if (cast_expr->target.name == "int") target_kind = 0;
    else if (cast_expr->target.name == "float") target_kind = 1;
    else if (cast_expr->target.name == "string") target_kind = 2;
    emit_operand(OpCode::CastTo, target_kind, cast_expr->location);

    if (cast_expr->fallback) {
      // CastTo pushes null on parse failure for fallible string→numeric. Test
      // for null without consuming the value: Dup, IsNull, JmpFalse over the
      // fallback (JmpFalse pops the bool). On the failure path, drop the null
      // and execute the fallback expression/block.
      emit(OpCode::Dup, cast_expr->location);
      emit(OpCode::IsNull, cast_expr->location);
      const std::size_t skip_fb = emit_jump(OpCode::JmpFalse, cast_expr->location);
      emit(OpCode::Pop, cast_expr->location);
      if (const auto *fb_expr = dynamic_cast<const ast::ExprStmt *>(cast_expr->fallback.get())) {
        compile_expr(*fb_expr->expr);
      } else {
        compile_stmt(*cast_expr->fallback);
      }
      patch_jump(skip_fb);
    }
    return;
  }

  if (const auto *ns_access = dynamic_cast<const ast::NamespaceAccessExpr *>(&expr)) {
    auto enum_it = enum_indices_.find(ns_access->namespace_name);
    if (enum_it != enum_indices_.end()) {
      int type_idx = enum_it->second;
      const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
      int variant_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
        if (meta.variants[static_cast<std::size_t>(i)] == ns_access->member_name) {
          variant_idx = i;
          break;
        }
      }
      if (variant_idx < 0) {
        error_at(ns_access->location, "Unknown enum variant '" + ns_access->member_name + "'.");
        return;
      }
      int param_count = meta.variant_param_counts[static_cast<std::size_t>(variant_idx)];
      if (param_count > 0) {
        error_at(ns_access->location, "Enum variant '" + ns_access->member_name + "' requires " +
                 std::to_string(param_count) + " argument(s).");
        return;
      }
      uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) |
                         static_cast<uint32_t>(variant_idx);
      emit_operand(OpCode::EnumVariant, operand, ns_access->location);
      return;
    }
    if (ns_access->namespace_name == "io" && used_.count("io") != 0) {
      NativeFn fn;
      if (ns_access->member_name == "out") {
        fn = NativeFn::IoOut;
      } else if (ns_access->member_name == "err") {
        fn = NativeFn::IoErr;
      } else if (ns_access->member_name == "in") {
        fn = NativeFn::IoIn;
      } else {
        error_at(ns_access->location, "Unknown io member '" + ns_access->member_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), ns_access->location);
      return;
    }
    if (ns_access->namespace_name == "fs" && used_.count("fs") != 0) {
      NativeFn fn;
      if (ns_access->member_name == "__read") {
        fn = NativeFn::FsRead;
      } else if (ns_access->member_name == "__write") {
        fn = NativeFn::FsWrite;
      } else {
        error_at(ns_access->location, "Unknown fs member '" + ns_access->member_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), ns_access->location);
      return;
    }
    if (ns_access->namespace_name == "sys" && used_.count("sys") != 0) {
      NativeFn fn;
      if (ns_access->member_name == "args") {
        fn = NativeFn::SysArgs;
      } else {
        error_at(ns_access->location, "Unknown sys member '" + ns_access->member_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), ns_access->location);
      return;
    }
    // Check trait namespaces for fully-qualified calls (Printable::to_string)
    if (trait_registry_.count(ns_access->namespace_name)) {
      // Defer to calling context: the callee will resolve the trait method
      // based on the argument type. For now, emit the member as a name lookup
      // so the call site can mangle it as Type::method.
      return;
    }
    // Check imported module namespaces
    if (imported_namespaces_.count(ns_access->namespace_name)) {
      std::string qualified = ns_access->namespace_name + "::" + ns_access->member_name;
      auto it = function_indices_.find(qualified);
      if (it != function_indices_.end()) {
        emit_constant(Value::function_value(
            chunk_.constants()[static_cast<std::size_t>(it->second)].function_index_storage),
            ns_access->location);
        return;
      }
      error_at(ns_access->location, "'" + ns_access->member_name + "' is not exported from module '" +
               ns_access->namespace_name + "'.");
      return;
    }
    if (used_.count(ns_access->namespace_name) != 0) {
      return;
    }
    if (ns_access->namespace_name == "io") {
      error_at(ns_access->location,
               "Module 'io' is not imported. Add 'using io;' at the top of the file.");
    } else {
      error_at(ns_access->location, "Unknown namespace '" + ns_access->namespace_name + "'.");
    }
    return;
  }

  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    int struct_idx = resolve_struct(struct_lit->struct_type);
    if (struct_idx < 0) {
      std::string type_name = struct_lit->struct_type.to_string();
      error_at(struct_lit->location, "Unknown struct type '" + type_name + "'.");
      return;
    }
    int type_idx = struct_idx;
    const auto &meta = chunk_.struct_metas()[static_cast<std::size_t>(type_idx)];
    for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
      if (i < struct_lit->fields.size()) {
        compile_expr(*struct_lit->fields[i].value);
      } else {
        emit(OpCode::Null, struct_lit->location);
      }
    }
    emit_operand(OpCode::StructNew, static_cast<uint32_t>((type_idx << 16) |
                 static_cast<int>(meta.field_names.size())), struct_lit->location);
    return;
  }

  if (const auto *field_access = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    if (const auto *ns_obj = dynamic_cast<const ast::NamespaceAccessExpr *>(field_access->object.get());
        ns_obj && ns_obj->namespace_name == "io" && used_.count("io") != 0) {
      NativeFn fn;
      if (ns_obj->member_name == "out" && field_access->field_name == "line") {
        fn = NativeFn::IoOutLine;
      } else if (ns_obj->member_name == "err" && field_access->field_name == "line") {
        fn = NativeFn::IoErrLine;
      } else if (ns_obj->member_name == "in" && field_access->field_name == "secret") {
        fn = NativeFn::IoInSecret;
      } else {
        error_at(field_access->location, "Unknown io method '" + ns_obj->member_name + "." + field_access->field_name + "'.");
        return;
      }
      emit_constant(Value::native_function_value(fn), field_access->location);
      return;
    }
    compile_expr(*field_access->object);
    uint32_t field_const = chunk_.add_constant(Value::string_value(field_access->field_name));
    emit_operand(OpCode::FieldGet, field_const, field_access->location);
    return;
  }

  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    compile_expr(*field_assign->object);
    compile_expr(*field_assign->value);
    uint32_t field_const = chunk_.add_constant(Value::string_value(field_assign->field_name));
    emit_operand(OpCode::FieldSet, field_const, field_assign->location);
    return;
  }

  if (const auto *array_lit = dynamic_cast<const ast::ArrayLiteralExpr *>(&expr)) {
    for (const ast::ExprPtr &element : array_lit->elements) {
      compile_expr(*element);
    }
    emit_operand(OpCode::ArrayNew, static_cast<uint32_t>(array_lit->elements.size()),
                 array_lit->location);
    return;
  }

  if (const auto *map_lit = dynamic_cast<const ast::MapLiteralExpr *>(&expr)) {
    for (std::size_t i = 0; i < map_lit->keys.size(); ++i) {
      compile_expr(*map_lit->keys[i]);
      compile_expr(*map_lit->values[i]);
    }
    emit_operand(OpCode::MapNew, static_cast<uint32_t>(map_lit->keys.size()),
                 map_lit->location);
    return;
  }

  if (const auto *index_expr = dynamic_cast<const ast::IndexExpr *>(&expr)) {
    compile_expr(*index_expr->object);
    compile_expr(*index_expr->index);
    emit(OpCode::IndexGet, index_expr->location);
    return;
  }

  if (const auto *index_assign = dynamic_cast<const ast::IndexAssignExpr *>(&expr)) {
    compile_expr(*index_assign->object);
    compile_expr(*index_assign->index);
    compile_expr(*index_assign->value);
    emit(OpCode::IndexSet, index_assign->location);
    return;
  }

  error_at(expr.location, "Unsupported expression in VM compiler.");
}

void Compiler::compile_assignment(const ast::AssignExpr &assign) {
  const int slot = resolve_local(assign.name);
  if (slot < 0) {
    error_at(assign.location, "Assignment to undeclared variable '" + assign.name + "'.");
    return;
  }
  if (!locals_[static_cast<std::size_t>(slot)].is_mutable) {
    error_at(assign.location, "Cannot assign to const variable '" + assign.name + "'.");
    return;
  }

  if (assign.op == ast::AssignOp::Assign) {
    compile_expr(*assign.value);
  } else {
    emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(slot), assign.location);
    compile_expr(*assign.value);
    switch (assign.op) {
    case ast::AssignOp::AddAssign:
      emit(OpCode::Add, assign.location);
      break;
    case ast::AssignOp::SubAssign:
      emit(OpCode::Subtract, assign.location);
      break;
    case ast::AssignOp::MulAssign:
      emit(OpCode::Multiply, assign.location);
      break;
    case ast::AssignOp::DivAssign:
      emit(OpCode::Divide, assign.location);
      break;
    default:
      error_at(assign.location, "Unsupported assignment operator.");
      return;
    }
  }

  emit_operand(OpCode::StoreLocal, static_cast<uint32_t>(slot), assign.location);
}

void Compiler::emit(OpCode op, ast::SourceLocation location) {
  chunk_.write(op, location.line, location.column);
}

void Compiler::emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location) {
  chunk_.write_operand(op, operand, location.line, location.column);
}

void Compiler::emit_constant(Value value, ast::SourceLocation location) {
  chunk_.write_constant(value, location.line, location.column);
}

std::size_t Compiler::emit_jump(OpCode op, ast::SourceLocation location) {
  emit_operand(op, 0, location);
  return chunk_.instructions().size() - 1;
}

void Compiler::patch_jump(std::size_t offset) {
  patch_jump_to(offset, chunk_.instructions().size());
}

void Compiler::patch_jump_to(std::size_t offset, std::size_t target) {
  const int32_t jump_offset = static_cast<int32_t>(target - offset - 1);
  Instruction &instruction = const_cast<Instruction &>(chunk_.instructions()[offset]);
  instruction.operand = jump_offset;
}

int Compiler::resolve_local(const std::string &name) const {
  for (std::size_t i = locals_.size(); i > 0; --i) {
    if (locals_[i - 1].name == name) {
      return static_cast<int>(i - 1);
    }
  }
  return -1;
}

bool Compiler::declare_local(const ast::VarDeclStmt &var_decl, uint32_t *slot) {
  if (resolve_local(var_decl.name) >= 0) {
    error_at(var_decl.location, "Duplicate variable declaration '" + var_decl.name + "'.");
    return false;
  }
  const bool is_mutable = var_decl.storage != "const";
  locals_.push_back(Local{
      .name = var_decl.name,
      .is_mutable = is_mutable,
  });
  *slot = static_cast<uint32_t>(locals_.size() - 1);
  return true;
}

int Compiler::resolve_struct(const ast::TypeExpr &type) {
  if (type.type_args.empty()) {
    auto it = struct_indices_.find(type.name);
    if (it != struct_indices_.end()) return it->second;
    return -1;
  }
  std::string mangled = type.name;
  for (const auto &arg : type.type_args) {
    mangled += "__" + arg.to_string();
  }
  auto it = struct_indices_.find(mangled);
  if (it != struct_indices_.end()) return it->second;
  auto gen_it = generic_struct_decls_.find(type.name);
  if (gen_it == generic_struct_decls_.end()) return -1;
  const ast::StructDecl *decl = gen_it->second;
  StructMeta meta;
  meta.name = mangled;
  for (const auto &field : decl->fields) {
    meta.field_names.push_back(field.name);
  }
  int idx = chunk_.add_struct_meta(std::move(meta));
  struct_indices_[mangled] = idx;
  return idx;
}

void Compiler::error_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(CompileError{
      .location = location,
      .message = std::move(message),
  });
}

void Compiler::warning_at(ast::SourceLocation location, std::string message) {
  warnings_.push_back(CompileWarning{
      .location = location,
      .message = std::move(message),
  });
}

void Compiler::process_import(const ast::ImportDecl &import_decl) {
  process_import_from(import_decl, "");
}

void Compiler::process_import_from(const ast::ImportDecl &import_decl,
                                    const std::string &importer_path) {
  if (!module_loader_) {
    error_at(import_decl.location, "Import not supported (no module loader configured).");
    return;
  }

  auto result = importer_path.empty()
                    ? module_loader_->load(import_decl.path)
                    : module_loader_->load_from(import_decl.path, importer_path);
  if (!result.module) {
    error_at(import_decl.location, result.error);
    return;
  }

  const ParsedModule &mod = *result.module;

  // Dedup by resolved path. If we've already processed this module via
  // some other import edge, every pub fn would otherwise get a *second*
  // FunctionInfo (entry=0) plus a fresh Function_ constant. The patch
  // pass walks fn bodies once per file, so the second registration's
  // entry stays 0; calls resolved through last-write-wins jump to byte 0
  // (= preamble) and recurse into main forever.
  //
  // Caveat: we don't currently re-bind the alias namespace on a second
  // import (e.g. `import "x" as a; import "x" as b;` won't make `b::foo`
  // resolve). No code in the tree exercises that today; revisit if it
  // does.
  if (!processed_module_paths_.insert(mod.resolved_path).second) {
    return;
  }

  // Recursively process imports declared inside the loaded module so that
  // types and functions it depends on are registered before we compile calls
  // into it (e.g. scanner.kl imports token.kl for the Token struct).
  if (mod.program) {
    for (const auto &inner_decl : mod.program->declarations) {
      if (const auto *inner_import = dynamic_cast<const ast::ImportDecl *>(inner_decl.get())) {
        process_import_from(*inner_import, mod.resolved_path);
      }
    }
  }

  std::string ns = import_decl.alias.empty() ? mod.namespace_name : import_decl.alias;

  imported_namespaces_.insert(ns);

  for (const auto *func : mod.public_functions) {
    if (!import_decl.selected_symbols.empty()) {
      bool found = false;
      for (const auto &s : import_decl.selected_symbols) {
        if (s == func->name) { found = true; break; }
      }
      if (!found) continue;
    }

    int idx = chunk_.add_function(FunctionInfo{
        .name = func->name,
        .entry = 0,
        .param_count = static_cast<int>(func->params.size()),
    });
    uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));

    std::string qualified = ns + "::" + func->name;
    function_indices_[qualified] = static_cast<int>(const_idx);

    if (!import_decl.selected_symbols.empty()) {
      function_indices_[func->name] = static_cast<int>(const_idx);
    }

    imported_function_decls_[ns].push_back(func);
  }

  // Also register private functions so pub functions can call them.
  for (const auto *func : mod.private_functions) {
    if (function_indices_.count(ns + "::" + func->name)) continue;
    int idx = chunk_.add_function(FunctionInfo{
        .name = func->name,
        .entry = 0,
        .param_count = static_cast<int>(func->params.size()),
    });
    uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
    function_indices_[ns + "::" + func->name] = static_cast<int>(const_idx);
    function_indices_[func->name] = static_cast<int>(const_idx);
    imported_function_decls_[ns].push_back(func);
  }

  // Register imported pub structs so struct literals and field access compile.
  for (const auto *sd : mod.public_structs) {
    if (!import_decl.selected_symbols.empty()) {
      bool found = false;
      for (const auto &s : import_decl.selected_symbols) {
        if (s == sd->name) { found = true; break; }
      }
      if (!found) continue;
    }
    if (sd->type_params.empty()) {
      StructMeta meta;
      meta.name = sd->name;
      for (const auto &field : sd->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[sd->name] = idx;
    }
  }

  // Register imported pub enums so enum variants compile.
  for (const auto *ed : mod.public_enums) {
    if (!import_decl.selected_symbols.empty()) {
      bool found = false;
      for (const auto &s : import_decl.selected_symbols) {
        if (s == ed->name) { found = true; break; }
      }
      if (!found) continue;
    }
    EnumMeta meta;
    meta.name = ed->name;
    for (const auto &v : ed->variants) {
      meta.variants.push_back(v.name);
      meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
    }
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_[ed->name] = idx;
  }

  // Register private structs/enums (needed by private helper functions).
  for (const auto *sd : mod.private_structs) {
    if (struct_indices_.count(sd->name)) continue;
    if (sd->type_params.empty()) {
      StructMeta meta;
      meta.name = sd->name;
      for (const auto &field : sd->fields) meta.field_names.push_back(field.name);
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[sd->name] = idx;
    }
  }
  for (const auto *ed : mod.private_enums) {
    if (enum_indices_.count(ed->name)) continue;
    EnumMeta meta;
    meta.name = ed->name;
    for (const auto &v : ed->variants) {
      meta.variants.push_back(v.name);
      meta.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
    }
    int idx = chunk_.add_enum_meta(std::move(meta));
    enum_indices_[ed->name] = idx;
  }
}

} // namespace kinglet
