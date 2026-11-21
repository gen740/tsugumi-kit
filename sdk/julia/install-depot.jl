using UUIDs

out = first(ARGS)
for i in 2:4:length(ARGS)
    name, uuid, tree, source = ARGS[i:i+3]
    slug = Base.version_slug(UUID(uuid), Base.SHA1(tree))
    dir = joinpath(out, "packages", name)
    mkpath(dir)
    symlink(source, joinpath(dir, slug))
end
