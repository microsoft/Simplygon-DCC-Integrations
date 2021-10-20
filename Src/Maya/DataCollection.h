// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

class Scene;
class MaterialHandler;

// Singleton instance used to store plugin wide data
class DataCollection
	{
	private:
		static DataCollection* instance;
		Scene *sceneHandler;
		MaterialHandler *materialHandler;
		DataCollection();

	public:
		float SceneRadius;
		~DataCollection();
		void SetSceneHandler(Scene*);
		Scene* GetSceneHandler() { return sceneHandler; }
		void SetMaterialHandler(MaterialHandler *);
		MaterialHandler* GetMaterialHandler() { return materialHandler; }
		static DataCollection* GetInstance();
	};