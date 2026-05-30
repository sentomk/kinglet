#pragma once

#include "ast/ast.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

struct ParsedModule {
  std::unique_ptr<ast::Program> program;
  std::string namespace_name;
  std::vector<const ast::FunctionDecl *> public_functions;
  std::vector<const ast::FunctionDecl *> private_functions;
  std::vector<const ast::StructDecl *> public_structs;
  std::vector<const ast::StructDecl *> private_structs;
  std::vector<const ast::EnumDecl *> public_enums;
  std::vector<const ast::EnumDecl *> private_enums;
};

class ModuleLoader {
public:
  explicit ModuleLoader(std::string base_dir);

  struct LoadResult {
    const ParsedModule *module = nullptr;
    std::string error;
  };

  void register_source_file(const std::string &path);
  LoadResult load(const std::string &path);

private:
  std::string resolve_path(const std::string &relative_path) const;
  std::string derive_namespace(const std::string &path) const;

  std::string base_dir_;
  std::unordered_map<std::string, ParsedModule> cache_;
  std::unordered_set<std::string> loading_;
  std::unordered_set<std::string> source_files_;
};

} // namespace kinglet
