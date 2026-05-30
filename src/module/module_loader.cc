#include "module/module_loader.h"

#include "lexer/scanner.h"
#include "parser/parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace kinglet {

ModuleLoader::ModuleLoader(std::string base_dir)
    : base_dir_(std::move(base_dir)) {}

std::string ModuleLoader::resolve_path(const std::string &relative_path) const {
  std::filesystem::path base(base_dir_);
  std::filesystem::path resolved = base / relative_path;
  return std::filesystem::canonical(resolved).string();
}

std::string ModuleLoader::derive_namespace(const std::string &path) const {
  std::filesystem::path p(path);
  return p.stem().string();
}

void ModuleLoader::register_source_file(const std::string &path) {
  std::filesystem::path p(path);
  std::filesystem::path target = p.is_absolute()
                                     ? p
                                     : std::filesystem::path(base_dir_) / path;
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::canonical(target, ec);
  if (ec) {
    // File may not exist on disk yet (e.g. unsaved LSP buffer); fall back to
    // a lexical normalization so registration never throws.
    canonical = std::filesystem::weakly_canonical(target, ec);
    if (ec) canonical = target.lexically_normal();
  }
  source_files_.insert(canonical.string());
}

ModuleLoader::LoadResult ModuleLoader::load(const std::string &path) {
  std::string resolved;
  try {
    resolved = resolve_path(path);
  } catch (const std::filesystem::filesystem_error &) {
    return {nullptr, "Cannot resolve import path: " + path};
  }

  if (source_files_.count(resolved)) {
    return {nullptr, "File cannot import itself: " + path};
  }

  if (loading_.count(resolved)) {
    return {nullptr, "Circular import detected: " + path};
  }

  auto it = cache_.find(resolved);
  if (it != cache_.end()) {
    return {&it->second, ""};
  }

  std::ifstream file(resolved, std::ios::in | std::ios::binary);
  if (!file) {
    return {nullptr, "Cannot open file: " + path};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  loading_.insert(resolved);

  Scanner scanner(std::move(source));
  auto tokens = scanner.scan_tokens();
  for (const auto &token : tokens) {
    if (token.type == TokenType::ERROR) {
      loading_.erase(resolved);
      return {nullptr, "Lexer error in " + path + ": " + std::string(token.lexeme)};
    }
  }

  Parser parser(tokens);
  auto parse_result = parser.parse();
  if (!parse_result.errors.empty()) {
    loading_.erase(resolved);
    return {nullptr, "Parse error in " + path + ": " + parse_result.errors[0].message};
  }

  ParsedModule mod;
  mod.namespace_name = derive_namespace(path);

  for (const auto &decl : parse_result.program->declarations) {
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      if (func->is_public) {
        mod.public_functions.push_back(func);
      } else {
        mod.private_functions.push_back(func);
      }
    } else if (const auto *sd = dynamic_cast<const ast::StructDecl *>(decl.get())) {
      if (sd->is_public) {
        mod.public_structs.push_back(sd);
      } else {
        mod.private_structs.push_back(sd);
      }
    } else if (const auto *ed = dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      if (ed->is_public) {
        mod.public_enums.push_back(ed);
      }
    }
  }

  mod.program = std::move(parse_result.program);
  loading_.erase(resolved);
  auto [inserted, _] = cache_.emplace(resolved, std::move(mod));
  return {&inserted->second, ""};
}

} // namespace kinglet
