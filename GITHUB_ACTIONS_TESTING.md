# GitHub Actions Testing Guide

This document describes how to test the automated build and release system for Moonlight Steam Deck.

## Workflow Overview

The repository includes an automated GitHub Actions workflow that:
- Builds Steam Deck Flatpak packages
- Creates release notes with new features
- Uploads artifacts to GitHub Actions
- Automatically creates releases when tags are pushed
- Provides manual trigger option for testing

## Testing Methods

### Method 1: Push to Branch (Automatic)

The workflow automatically triggers on pushes to these branches:
- `master`
- `main`
- `cursor/**` (e.g., `cursor/auto-reconnect-and-steam-deck-build-223a`)
- `feature/**`

**To test:**
```bash
# Make changes and commit
git add .
git commit -m "Your commit message"

# Push to trigger workflow
git push origin your-branch-name
```

**Expected result:**
- Workflow runs automatically
- Builds Flatpak package
- Uploads artifact to Actions tab
- Does NOT create a release (only artifacts)

### Method 2: Manual Workflow Dispatch

Trigger the workflow manually from the GitHub Actions tab.

**Steps:**
1. Go to GitHub repository
2. Click "Actions" tab
3. Select "Build Steam Deck Flatpak" workflow
4. Click "Run workflow" button
5. Choose branch
6. Optionally set custom version
7. Optionally enable "Create release"

**Options:**
- **version**: Custom version string (e.g., "v6.1.1-test")
- **create_release**: Set to "true" to create a GitHub release

**Expected result:**
- Builds Flatpak with specified version
- Creates prerelease if "create_release" is true
- Uploads artifacts

### Method 3: Tag Push (Production Release)

Create an official release by pushing a version tag.

**Steps:**
```bash
# Create and push a tag
git tag v6.1.1
git push origin v6.1.1
```

**Expected result:**
- Workflow runs automatically
- Builds Flatpak package
- Creates official GitHub release (not prerelease)
- Marks release as "latest"
- Includes both .flatpak file and release notes

## Workflow Validation

### Pre-Push Validation

Before pushing, validate locally:

```bash
# Validate YAML syntax
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build-steamdeck-flatpak.yml'))"

# Validate shell scripts
bash -n build-steamdeck.sh
bash -n scripts/build-steamdeck-flatpak.sh

# Check for linter errors
# (if applicable to your code changes)
```

### Post-Push Verification

After pushing, verify the workflow:

1. **Check Actions Tab**
   - Go to GitHub repository → Actions
   - Find your workflow run
   - Verify all steps complete successfully (green checkmarks)

2. **Check Build Summary**
   - Click on the workflow run
   - Review the build summary at the bottom
   - Verify version, size, and feature list

3. **Download Artifact**
   - Click on the artifact name in workflow run
   - Download the .flatpak file
   - Verify file size (should be 50-150MB)

4. **Verify Release (if tag pushed)**
   - Go to Releases tab
   - Find your release
   - Verify release notes are complete
   - Download .flatpak file
   - Check that file is downloadable

## Testing Checklist

### Build Test
- [ ] Workflow triggers successfully
- [ ] All dependencies install correctly
- [ ] Flatpak builds without errors
- [ ] Bundle creation succeeds
- [ ] Artifact uploads successfully
- [ ] Build summary appears correctly

### Release Test (Tag Push)
- [ ] Release is created automatically
- [ ] Release title is correct
- [ ] Release notes are complete and formatted
- [ ] .flatpak file is attached
- [ ] Release notes file is attached
- [ ] Release is marked as "latest"

### Artifact Test
- [ ] Artifact can be downloaded
- [ ] File size is reasonable (50-150MB)
- [ ] Filename includes version
- [ ] Release notes file is included

### Feature Verification
- [ ] Release notes mention auto-reconnect
- [ ] Installation instructions are clear
- [ ] Troubleshooting section is complete
- [ ] All emojis render correctly
- [ ] Links are valid

## Common Issues and Solutions

### Issue: Workflow doesn't trigger

**Solution:**
- Verify branch name matches trigger patterns
- Check workflow file syntax
- Ensure `.github/workflows/` directory exists
- Verify workflow file has `.yml` extension

### Issue: Build fails at Flatpak step

**Solution:**
- Check runtime versions in manifest
- Verify all submodules are initialized
- Check build logs for specific errors
- Try building locally with `make steamdeck`

### Issue: Release not created

**Solution:**
- Verify tag format starts with 'v' (e.g., v6.1.1)
- Check GITHUB_TOKEN permissions
- Verify workflow conditional: `startsWith(github.ref, 'refs/tags/')`
- Check workflow dispatch has `create_release: true`

### Issue: Artifact too large or too small

**Solution:**
- Too large (>200MB): Check for debug symbols, unnecessary files
- Too small (<30MB): Build may be incomplete, check build logs
- Normal range: 50-150MB

## Manual Testing Commands

### Local Build Test
```bash
# Test the build script
./build-steamdeck.sh

# Or use make
make steamdeck

# Clean build
make clean && make steamdeck
```

### Local Workflow Validation
```bash
# Install act (GitHub Actions local runner)
# https://github.com/nektos/act
curl https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash

# Run workflow locally
act push -W .github/workflows/build-steamdeck-flatpak.yml
```

## Continuous Integration Status

The workflow is configured to:
- ✅ Build on every push to development branches
- ✅ Build on every pull request to master/main
- ✅ Create releases automatically on tag push
- ✅ Support manual triggering with custom options
- ✅ Upload artifacts with 30-day retention
- ✅ Provide detailed build summaries

## Success Criteria

A successful workflow run should:
1. Complete in 15-45 minutes (depending on runner speed)
2. Produce a .flatpak file of 50-150MB
3. Generate complete release notes
4. Pass all validation steps
5. Show green checkmarks for all steps
6. Create a release (if triggered by tag)

## Next Steps After Testing

Once workflow is verified:
1. Merge changes to main branch
2. Create production tag (e.g., v6.1.1)
3. Push tag to trigger official release
4. Announce release with download link
5. Monitor for issues and user feedback

## Support

If you encounter issues:
- Check workflow logs in GitHub Actions tab
- Review this testing guide
- Check QUICK_START_STEAM_DECK.md for build info
- Create an issue in the repository
